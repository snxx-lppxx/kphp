#include "runtime/rpc.h"

#include <cstdarg>

#include "auto/TL/constants/common.h"
#include "common/rpc-error-codes.h"

#include "runtime/critical_section.h"
#include "runtime/exception.h"
#include "runtime/memcache.h"
#include "runtime/misc.h"
#include "runtime/net_events.h"
#include "runtime/resumable.h"
#include "runtime/string_functions.h"
#include "runtime/tl/rpc_function.h"
#include "runtime/tl/rpc_query.h"
#include "runtime/tl/rpc_request.h"
#include "runtime/tl/tl_builtins.h"
#include "runtime/zlib.h"
#include "server/php-queries.h"

static const int GZIP_PACKED = 0x3072cfa1;

static const string UNDERSCORE("_", 1);
static const string STR_ERROR("__error", 7);
static const string STR_ERROR_CODE("__error_code", 12);

static const char *last_rpc_error;

static const int *rpc_data_begin;
static const int *rpc_data;
static int rpc_data_len;
static string rpc_data_copy;
static string rpc_filename;

static const int *rpc_data_begin_backup;
static const int *rpc_data_backup;
static int rpc_data_len_backup;
static string rpc_data_copy_backup;

tl_fetch_wrapper_ptr tl_fetch_wrapper;
array<tl_storer_ptr> tl_storers_ht;

template<class T>
static inline T store_parse_number(const string &v) {
  T result = 0;
  const char *s = v.c_str();
  int sign = 1;
  if (*s == '-') {
    s++;
    sign = -1;
  }
  while ('0' <= *s && *s <= '9') {
    result = result * 10 + (*s++ - '0');
  }
  return result * sign;
}

template<class T>
static inline T store_parse_number(const var &v) {
  if (!v.is_string()) {
    if (v.is_float()) {
      return (T)v.to_float();
    }
    return (T)v.to_int();
  }
  return store_parse_number<T>(v.to_string());
}


template<class T>
static inline T store_parse_number_unsigned(const string &v) {
  T result = 0;
  const char *s = v.c_str();
  while ('0' <= *s && *s <= '9') {
    result = result * 10 + (*s++ - '0');
  }
  return result;
}

template<class T>
static inline T store_parse_number_unsigned(const var &v) {
  if (!v.is_string()) {
    if (v.is_float()) {
      return (T)v.to_float();
    }
    return (T)v.to_int();
  }
  return store_parse_number_unsigned<T>(v.to_string());
}

template<class T>
static inline T store_parse_number_hex(const string &v) {
  T result = 0;
  const char *s = v.c_str();
  while (true) {
    T next = -1;
    if ('0' <= *s && *s <= '9') {
      next = *s - '0';
    } else if ('A' <= *s && *s <= 'F') {
      next = *s - ('A' - 10);
    } else if ('a' <= *s && *s <= 'f') {
      next = *s - ('a' - 10);
    }
    if (next == (T)-1) {
      break;
    }

    result = result * 16 + next;
    s++;
  }
  return result;
}


static void rpc_parse_save_backup() {
  dl::enter_critical_section();//OK
  rpc_data_copy_backup = rpc_data_copy;
  dl::leave_critical_section();

  rpc_data_begin_backup = rpc_data_begin;
  rpc_data_backup = rpc_data;
  rpc_data_len_backup = rpc_data_len;
}

void rpc_parse_restore_previous() {
  php_assert ((rpc_data_copy_backup.size() & 3) == 0);

  dl::enter_critical_section();//OK
  rpc_data_copy = rpc_data_copy_backup;
  rpc_data_copy_backup = UNDERSCORE;//for assert
  dl::leave_critical_section();

  rpc_data_begin = rpc_data_begin_backup;
  rpc_data = rpc_data_backup;
  rpc_data_len = rpc_data_len_backup;
}

const char *last_rpc_error_get() {
  return last_rpc_error;
}

void last_rpc_error_reset() {
  last_rpc_error = nullptr;
}

void rpc_parse(const int *new_rpc_data, int new_rpc_data_len) {
  rpc_parse_save_backup();

  rpc_data_begin = new_rpc_data;
  rpc_data = new_rpc_data;
  rpc_data_len = new_rpc_data_len;
}

bool f$rpc_parse(const string &new_rpc_data) {
  if (new_rpc_data.size() % sizeof(int) != 0) {
    php_warning("Wrong parameter \"new_rpc_data\" of len %d passed to function rpc_parse", (int)new_rpc_data.size());
    last_rpc_error = "Result's length is not divisible by 4";
    return false;
  }

  rpc_parse_save_backup();

  dl::enter_critical_section();//OK
  rpc_data_copy = new_rpc_data;
  dl::leave_critical_section();

  rpc_data_begin = rpc_data = reinterpret_cast <const int *> (rpc_data_copy.c_str());
  rpc_data_len = rpc_data_copy.size() / sizeof(int);
  return true;
}

bool f$rpc_parse(const var &new_rpc_data) {
  if (!new_rpc_data.is_string()) {
    php_warning("Parameter 1 of function rpc_parse must be a string, %s is given", new_rpc_data.get_type_c_str());
    return false;
  }

  return f$rpc_parse(new_rpc_data.to_string());
}

bool f$rpc_parse(bool new_rpc_data) {
  return f$rpc_parse(var{new_rpc_data});
}

bool f$rpc_parse(const Optional<string> &new_rpc_data) {
  auto rpc_parse_lambda = [](const auto &v) { return f$rpc_parse(v); };
  return call_fun_on_optional_value(rpc_parse_lambda, new_rpc_data);
}

int rpc_get_pos() {
  return (int)(long)(rpc_data - rpc_data_begin);
}

bool rpc_set_pos(int pos) {
  if (pos < 0 || rpc_data_begin + pos > rpc_data) {
    return false;
  }

  rpc_data_len += (int)(rpc_data - rpc_data_begin - pos);
  rpc_data = rpc_data_begin + pos;
  return true;
}


static inline void check_rpc_data_len(int len) {
  if (rpc_data_len < len) {
    THROW_EXCEPTION(new_Exception(rpc_filename, __LINE__, string("Not enough data to fetch", 24), -1));
    return;
  }
  rpc_data_len -= len;
}

int rpc_lookup_int() {
  TRY_CALL_VOID(int, (check_rpc_data_len(1)));
  rpc_data_len++;
  return *rpc_data;
}

int f$fetch_int() {
  TRY_CALL_VOID(int, (check_rpc_data_len(1)));
  return *rpc_data++;
}

UInt f$fetch_UInt() {
  TRY_CALL_VOID(UInt, (check_rpc_data_len(1)));
  return UInt((unsigned int)(*rpc_data++));
}

Long f$fetch_Long() {
  TRY_CALL_VOID(Long, (check_rpc_data_len(2)));
  long long result = *(long long *)rpc_data;
  rpc_data += 2;

  return Long(result);
}

ULong f$fetch_ULong() {
  TRY_CALL_VOID(ULong, (check_rpc_data_len(2)));
  unsigned long long result = *(unsigned long long *)rpc_data;
  rpc_data += 2;

  return ULong(result);
}

var f$fetch_unsigned_int() {
  TRY_CALL_VOID(var, (check_rpc_data_len(1)));
  unsigned int result = *rpc_data++;

  if (result <= (unsigned int)INT_MAX) {
    return (int)result;
  }

  return f$strval(UInt(result));
}

var f$fetch_long() {
  TRY_CALL_VOID(var, (check_rpc_data_len(2)));
  long long result = *(long long *)rpc_data;
  rpc_data += 2;

  if ((long long)INT_MIN <= result && result <= (long long)INT_MAX) {
    return (int)result;
  }

  return f$strval(Long(result));
}

var f$fetch_unsigned_long() {
  TRY_CALL_VOID(var, (check_rpc_data_len(2)));
  unsigned long long result = *(unsigned long long *)rpc_data;
  rpc_data += 2;

  if (result <= (unsigned long long)INT_MAX) {
    return (int)result;
  }

  return f$strval(ULong(result));
}

string f$fetch_unsigned_int_hex() {
  TRY_CALL_VOID(string, (check_rpc_data_len(1)));
  unsigned int result = *rpc_data++;

  char buf[8], *end_buf = buf + 8;
  for (int i = 0; i < 8; i++) {
    *--end_buf = lhex_digits[result & 15];
    result >>= 4;
  }

  return string(end_buf, 8);
}

string f$fetch_unsigned_long_hex() {
  TRY_CALL_VOID(string, (check_rpc_data_len(2)));
  unsigned long long result = *(unsigned long long *)rpc_data;
  rpc_data += 2;

  char buf[16], *end_buf = buf + 16;
  for (int i = 0; i < 16; i++) {
    *--end_buf = lhex_digits[result & 15];
    result >>= 4;
  }

  return string(end_buf, 16);
}

string f$fetch_unsigned_int_str() {
  return f$strval(TRY_CALL (UInt, string, (f$fetch_UInt())));
}

string f$fetch_unsigned_long_str() {
  return f$strval(TRY_CALL (ULong, string, (f$fetch_ULong())));
}

double f$fetch_double() {
  TRY_CALL_VOID(double, (check_rpc_data_len(2)));
  double result = *(double *)rpc_data;
  rpc_data += 2;

  return result;
}

void f$fetch_raw_vector_int(array<int> &out, int n_elems) {
  int rpc_data_buf_offset = sizeof(int) * n_elems / 4;
  TRY_CALL_VOID(void, (check_rpc_data_len(rpc_data_buf_offset)));
  out.memcpy_vector(n_elems, rpc_data);
  rpc_data += rpc_data_buf_offset;
}

void f$fetch_raw_vector_double(array<double> &out, int n_elems) {
  int rpc_data_buf_offset = sizeof(double) * n_elems / 4;
  TRY_CALL_VOID(void, (check_rpc_data_len(rpc_data_buf_offset)));
  out.memcpy_vector(n_elems, rpc_data);
  rpc_data += rpc_data_buf_offset;
}

static inline const char *f$fetch_string_raw(int *string_len) {
  TRY_CALL_VOID_(check_rpc_data_len(1), return nullptr);
  const char *str = reinterpret_cast <const char *> (rpc_data);
  int result_len = (unsigned char)*str++;
  if (result_len < 254) {
    TRY_CALL_VOID_(check_rpc_data_len(result_len >> 2), return nullptr);
    rpc_data += (result_len >> 2) + 1;
  } else if (result_len == 254) {
    result_len = (unsigned char)str[0] + ((unsigned char)str[1] << 8) + ((unsigned char)str[2] << 16);
    str += 3;
    TRY_CALL_VOID_(check_rpc_data_len((result_len + 3) >> 2), return nullptr);
    rpc_data += ((result_len + 7) >> 2);
  } else {
    THROW_EXCEPTION(new_Exception(rpc_filename, __LINE__, string("Can't fetch string, 255 found", 29), -3));
    return nullptr;
  }

  *string_len = result_len;
  return str;
}

string f$fetch_string() {
  int result_len = 0;
  const char *str = TRY_CALL(const char*, string, f$fetch_string_raw(&result_len));
  return string(str, result_len);
}

int f$fetch_string_as_int() {
  int result_len = 0;
  const char *str = TRY_CALL(const char*, int, f$fetch_string_raw(&result_len));
  return string::to_int(str, result_len);
}

var f$fetch_memcache_value() {
  int res = TRY_CALL(int, bool, f$fetch_int());
  switch (res) {
    case MEMCACHE_VALUE_STRING: {
      int value_len = 0;
      const char *value = TRY_CALL(const char*, bool, f$fetch_string_raw(&value_len));
      int flags = TRY_CALL(int, bool, f$fetch_int());
      return mc_get_value(value, value_len, flags);
    }
    case MEMCACHE_VALUE_LONG: {
      var value = TRY_CALL(var, bool, f$fetch_long());
      int flags = TRY_CALL(int, bool, f$fetch_int());

      if (flags != 0) {
        php_warning("Wrong parameter flags = %d returned in Memcache::get", flags);
      }

      return value;
    }
    case MEMCACHE_VALUE_NOT_FOUND: {
      return false;
    }
    default: {
      php_warning("Wrong memcache.Value constructor = %x", res);
      THROW_EXCEPTION(new_Exception(rpc_filename, __LINE__, string("Wrong memcache.Value constructor"), -1));
      return var();
    }
  }
}

bool f$fetch_eof() {
  return rpc_data_len == 0;
}

bool f$fetch_end() {
  if (rpc_data_len) {
    THROW_EXCEPTION(new_Exception(rpc_filename, __LINE__, string("Too much data to fetch"), -2));
    return false;
  }
  return true;
}

rpc_connection::rpc_connection() :
  bool_value(false),
  host_num(-1),
  port(-1),
  timeout_ms(-1),
  default_actor_id(-1),
  connect_timeout(-1),
  reconnect_timeout(-1) {
}

rpc_connection::rpc_connection(bool value) :
  bool_value(value),
  host_num(-1),
  port(-1),
  timeout_ms(-1),
  default_actor_id(-1),
  connect_timeout(-1),
  reconnect_timeout(-1) {
}

rpc_connection::rpc_connection(bool value, int host_num, int port, int timeout_ms, long long default_actor_id, int connect_timeout, int reconnect_timeout) :
  bool_value(value),
  host_num(host_num),
  port(port),
  timeout_ms(timeout_ms),
  default_actor_id(default_actor_id),
  connect_timeout(connect_timeout),
  reconnect_timeout(reconnect_timeout) {
}

rpc_connection &rpc_connection::operator=(bool value) {
  bool_value = value;
  return *this;
}


rpc_connection f$new_rpc_connection(const string &host_name, int port, const var &default_actor_id, double timeout, double connect_timeout, double reconnect_timeout) {
  int host_num = rpc_connect_to(host_name.c_str(), port);
  if (host_num < 0) {
    return rpc_connection();
  }

  return rpc_connection(true, host_num, port, timeout_convert_to_ms(timeout),
                        store_parse_number<long long>(default_actor_id),
                        timeout_convert_to_ms(connect_timeout), timeout_convert_to_ms(reconnect_timeout));
}

bool f$boolval(const rpc_connection &my_rpc) {
  return my_rpc.bool_value;
}

bool eq2(const rpc_connection &my_rpc, bool value) {
  return my_rpc.bool_value == value;
}

bool eq2(bool value, const rpc_connection &my_rpc) {
  return value == my_rpc.bool_value;
}

bool equals(bool value, const rpc_connection &my_rpc) {
  return equals(value, my_rpc.bool_value);
}

bool equals(const rpc_connection &my_rpc, bool value) {
  return equals(my_rpc.bool_value, value);
}


static string_buffer data_buf;
static const int data_buf_header_size = 2 * sizeof(long long) + 4 * sizeof(int);
static const int data_buf_header_reserved_size = sizeof(long long) + sizeof(int);

int rpc_stored;
static int rpc_pack_threshold;
static int rpc_pack_from;

void estimate_and_flush_overflow(int &bytes_sent) {
  // estimate
  bytes_sent += data_buf.size();
  if (bytes_sent >= (1 << 15) && bytes_sent > data_buf.size()) {
    f$rpc_flush();
    bytes_sent = data_buf.size();
  }
}

void f$store_gzip_pack_threshold(int pack_threshold_bytes) {
  rpc_pack_threshold = pack_threshold_bytes;
}

void f$store_start_gzip_pack() {
  rpc_pack_from = data_buf.size();
}

void f$store_finish_gzip_pack(int threshold) {
  if (rpc_pack_from != -1 && threshold > 0) {
    int answer_size = data_buf.size() - rpc_pack_from;
    php_assert (rpc_pack_from % sizeof(int) == 0 && 0 <= rpc_pack_from && 0 <= answer_size);
    if (answer_size >= threshold) {
      const char *answer_begin = data_buf.c_str() + rpc_pack_from;
      const string_buffer *compressed = zlib_encode(answer_begin, answer_size, 6, ZLIB_ENCODE);

      if ((int)(compressed->size() + 2 * sizeof(int)) < answer_size) {
        data_buf.set_pos(rpc_pack_from);
        f$store_int(GZIP_PACKED);
        store_string(compressed->buffer(), compressed->size());
      }
    }
  }
  rpc_pack_from = -1;
}


template<class T>
inline bool store_raw(T v) {
  data_buf.append((char *)&v, sizeof(v));
  return true;
}

bool f$store_raw(const string &data) {
  int data_len = (int)data.size();
  if (data_len & 3) {
    return false;
  }
  data_buf.append(data.c_str(), data_len);
  return true;
}

void f$store_raw_vector_int(const array<int> &vector) {
  data_buf.append(reinterpret_cast<const char *>(vector.get_const_vector_pointer()), sizeof(int) * vector.count());
}

void f$store_raw_vector_double(const array<double> &vector) {
  data_buf.append(reinterpret_cast<const char *>(vector.get_const_vector_pointer()), sizeof(double) * vector.count());
}

bool store_header(long long cluster_id, int flags) {
  if (flags) {
    f$store_int(TL_RPC_DEST_ACTOR_FLAGS);
    store_long(cluster_id);
    f$store_int(flags);
  } else {
    f$store_int(TL_RPC_DEST_ACTOR);
    store_long(cluster_id);
  }
  return true;
}

bool f$store_header(const var &cluster_id, int flags) {
  return store_header(store_parse_number<long long>(cluster_id), flags);
}

bool store_error(int error_code, const char *error_text, int error_text_len) {
  f$rpc_clean(true);
  f$store_int(error_code);
  store_string(error_text, error_text_len);
  rpc_store(true);
  script_error();
  return true;
}

bool store_error(int error_code, const char *error_text) {
  return store_error(error_code, error_text, (int)strlen(error_text));
}

bool f$store_error(int error_code, const string &error_text) {
  return store_error(error_code, error_text.c_str(), (int)error_text.size());
}

bool f$store_int(int v) {
  return store_raw(v);
}

bool f$store_UInt(UInt v) {
  return store_raw(v.l);
}

bool f$store_Long(Long v) {
  return store_raw(v.l);
}

bool f$store_ULong(ULong v) {
  return store_raw(v.l);
}

bool store_unsigned_int(unsigned int v) {
  return store_raw(v);
}

bool store_long(long long v) {
  return store_raw(v);
}

bool store_unsigned_long(unsigned long long v) {
  return store_raw(v);
}

bool f$store_unsigned_int(const string &v) {
  return store_raw(store_parse_number_unsigned<unsigned int>(v));
}

bool f$store_long(const string &v) {
  return store_raw(store_parse_number<long long>(v));
}

bool f$store_unsigned_long(const string &v) {
  return store_raw(store_parse_number_unsigned<unsigned long long>(v));
}

bool f$store_unsigned_int_hex(const string &v) {
  return store_raw(store_parse_number_hex<unsigned int>(v));
}

bool f$store_unsigned_long_hex(const string &v) {
  return store_raw(store_parse_number_hex<unsigned long long>(v));
}

bool f$store_double(double v) {
  return store_raw(v);
}

bool store_string(const char *v, int v_len) {
  int all_len = v_len;
  if (v_len < 254) {
    data_buf << (char)(v_len);
    all_len += 1;
  } else if (v_len < (1 << 24)) {
    data_buf
      << (char)(254)
      << (char)(v_len & 255)
      << (char)((v_len >> 8) & 255)
      << (char)((v_len >> 16) & 255);
    all_len += 4;
  } else {
    php_critical_error ("trying to store too big string of length %d", v_len);
  }
  data_buf.append(v, v_len);

  while (all_len % 4 != 0) {
    data_buf << '\0';
    all_len++;
  }
  return true;
}

bool f$store_string(const string &v) {
  return store_string(v.c_str(), (int)v.size());
}

bool f$store_many(const array<var> &a) {
  int n = a.count();
  if (n == 0) {
    php_warning("store_many must take at least 1 argument");
    return false;
  }

  string pattern = a.get_value(0).to_string();
  if (n != 1 + (int)pattern.size()) {
    php_warning("Wrong number of arguments in call to store_many");
    return false;
  }

  for (int i = 1; i < n; i++) {
    switch (pattern[i - 1]) {
      case 's':
        f$store_string(a.get_value(i).to_string());
        break;
      case 'l':
        f$store_long(a.get_value(i));
        break;
      case 'd':
      case 'i':
        f$store_int(a.get_value(i).to_int());
        break;
      case 'f':
        f$store_double(a.get_value(i).to_float());
        break;
      default:
        php_warning("Wrong symbol '%c' at position %d in first argument of store_many", pattern[i - 1], i - 1);
        break;
    }
  }

  return true;
}


bool f$store_finish() {
  return rpc_store(false);
}

bool f$rpc_clean(bool is_error) {
  data_buf.clean();
  f$store_int(-1); //reserve for TL_RPC_DEST_ACTOR
  store_long(-1); //reserve for actor_id
  f$store_int(-1); //reserve for length
  f$store_int(-1); //reserve for num
  f$store_int(-is_error); //reserve for type
  store_long(-1); //reserve for req_id

  rpc_pack_from = -1;
  return true;
}

string f$rpc_get_clean() {
  string data = string(data_buf.c_str() + data_buf_header_size, (int)(data_buf.size() - data_buf_header_size));
  f$rpc_clean();
  return data;
}

string f$rpc_get_contents() {
  return string(data_buf.c_str() + data_buf_header_size, (int)(data_buf.size() - data_buf_header_size));
}


bool rpc_store(bool is_error) {
  if (rpc_stored) {
    return false;
  }

  if (!is_error) {
    rpc_pack_from = data_buf_header_size;
    f$store_finish_gzip_pack(rpc_pack_threshold);
  }

  f$store_int(-1); // reserve for crc32
  rpc_stored = 1;
  rpc_answer(data_buf.c_str() + data_buf_header_reserved_size, (int)(data_buf.size() - data_buf_header_reserved_size));
  return true;
}


struct rpc_request {
  int resumable_id; // == 0 - default, > 0 if not finished, -1 if received an answer, -2 if received an error, -3 if answer was gotten
  union {
    event_timer *timer;
    char *answer;
    const char *error;
  };
};


// only for good linkage. Will be never used to load
template<>
int Storage::tagger<rpc_request>::get_tag() {
  return 1960913044;
}

static rpc_request *rpc_requests;
static int rpc_requests_size;
static long long rpc_requests_last_query_num;

static slot_id_t rpc_first_request_id;
static slot_id_t rpc_first_array_request_id;
static slot_id_t rpc_next_request_id;
static slot_id_t rpc_first_unfinished_request_id;

static rpc_request gotten_rpc_request;

static int timeout_wakeup_id = -1;

static inline rpc_request *get_rpc_request(slot_id_t request_id) {
  php_assert (rpc_first_request_id <= request_id && request_id < rpc_next_request_id);
  if (request_id < rpc_first_array_request_id) {
    return &gotten_rpc_request;
  }
  return &rpc_requests[request_id - rpc_first_array_request_id];
}

class rpc_resumable : public Resumable {
private:
  int request_id;
  int port;
  long long actor_id;
  double begin_time;

protected:
  bool run() {
    php_assert (dl::query_num == rpc_requests_last_query_num);
    rpc_request *request = get_rpc_request(request_id);
    php_assert (request->resumable_id < 0);
    php_assert (input_ == nullptr);

/*
    if (request->resumable_id == -1) {
      int len = *reinterpret_cast <int *>(request->answer - 12);
      fprintf (stderr, "Receive  string of len %d at %p\n", len, request->answer);
      for (int i = -12; i <= len; i++) {
        fprintf (stderr, "%d: %x(%d)\t%c\n", i, request->answer[i], request->answer[i], request->answer[i] >= 32 ? request->answer[i] : '.');
      }
    }
*/
    if (rpc_first_unfinished_request_id == request_id) {
      while (rpc_first_unfinished_request_id < rpc_next_request_id &&
             get_rpc_request(rpc_first_unfinished_request_id)->resumable_id < 0) {
        rpc_first_unfinished_request_id++;
      }
      if (rpc_first_unfinished_request_id < rpc_next_request_id) {
        int resumable_id = get_rpc_request(rpc_first_unfinished_request_id)->resumable_id;
        php_assert (resumable_id > 0);
        const Resumable *resumable = get_forked_resumable(resumable_id);
        php_assert (resumable != nullptr);
        static_cast <const rpc_resumable *>(resumable)->set_server_status_rpc();
      } else {
        ::set_server_status_rpc(0, 0, get_precise_now());
      }
    }

    request_id = -1;
    output_->save<rpc_request>(*request);
    php_assert (request->resumable_id == -2 || request->resumable_id == -1);
    request->resumable_id = -3;
    request->answer = nullptr;

    return true;
  }

  void set_server_status_rpc() const {
    ::set_server_status_rpc(port, actor_id, begin_time);
  }

public:
  rpc_resumable(int request_id, int port, long long actor_id) :
    request_id(request_id),
    port(port),
    actor_id(actor_id),
    begin_time(get_precise_now()) {
    if (rpc_first_unfinished_request_id == request_id) {
      set_server_status_rpc();
    }
  }
};

static array<double> rpc_request_need_timer;

static void process_rpc_timeout(int request_id) {
  process_rpc_error(request_id, TL_ERROR_QUERY_TIMEOUT, "Timeout in KPHP runtime");
}

static void process_rpc_timeout(event_timer *timer) {
  return process_rpc_timeout(timer->wakeup_extra);
}

int rpc_send(const rpc_connection &conn, double timeout, bool ignore_answer) {
  if (unlikely (conn.host_num < 0)) {
    php_warning("Wrong rpc_connection specified");
    return -1;
  }

  if (timeout <= 0 || timeout > MAX_TIMEOUT) {
    timeout = conn.timeout_ms * 0.001;
  }

  f$store_int(-1); // reserve for crc32
  php_assert (data_buf.size() % sizeof(int) == 0);

  int reserved = data_buf_header_reserved_size;
  if (conn.default_actor_id) {
    const char *answer_begin = data_buf.c_str() + data_buf_header_size;
    int x = *(int *)answer_begin;
    if (x != TL_RPC_DEST_ACTOR && x != TL_RPC_DEST_ACTOR_FLAGS) {
      reserved -= (int)(sizeof(int) + sizeof(long long));
      php_assert (reserved >= 0);
      *(int *)(answer_begin - sizeof(int) - sizeof(long long)) = TL_RPC_DEST_ACTOR;
      *(long long *)(answer_begin - sizeof(long long)) = conn.default_actor_id;
    }
  }

  dl::size_type request_size = (dl::size_type)(data_buf.size() - reserved);
  void *p = dl::allocate(request_size);
  memcpy(p, data_buf.c_str() + reserved, request_size);

  slot_id_t result = rpc_send_query(conn.host_num, (char *)p, (int)request_size, timeout_convert_to_ms(timeout));
  if (result <= 0) {
    return -1;
  }

  if (dl::query_num != rpc_requests_last_query_num) {
    rpc_requests_last_query_num = dl::query_num;
    rpc_requests_size = 170;
    rpc_requests = static_cast <rpc_request *> (dl::allocate(sizeof(rpc_request) * rpc_requests_size));

    rpc_first_request_id = result;
    rpc_first_array_request_id = result;
    rpc_next_request_id = result + 1;
    rpc_first_unfinished_request_id = result;
    gotten_rpc_request.resumable_id = -3;
    gotten_rpc_request.answer = nullptr;
  } else {
    php_assert (rpc_next_request_id == result);
    rpc_next_request_id++;
  }

  if (result - rpc_first_array_request_id >= rpc_requests_size) {
    php_assert (result - rpc_first_array_request_id == rpc_requests_size);
    if (rpc_first_unfinished_request_id > rpc_first_array_request_id + rpc_requests_size / 2) {
      memcpy(rpc_requests,
             rpc_requests + rpc_first_unfinished_request_id - rpc_first_array_request_id,
             sizeof(rpc_request) * (rpc_requests_size - (rpc_first_unfinished_request_id - rpc_first_array_request_id)));
      rpc_first_array_request_id = rpc_first_unfinished_request_id;
    } else {
      rpc_requests = static_cast <rpc_request *> (dl::reallocate(rpc_requests, sizeof(rpc_request) * 2 * rpc_requests_size, sizeof(rpc_request) * rpc_requests_size));
      rpc_requests_size *= 2;
    }
  }

  rpc_request *cur = get_rpc_request(result);

  cur->resumable_id = register_forked_resumable(new rpc_resumable(result, conn.port, conn.default_actor_id));
  cur->timer = nullptr;
  if (ignore_answer) {
    int resumable_id = cur->resumable_id;
    process_rpc_timeout(result);
    get_forked_storage(resumable_id)->load<rpc_request>();
    return resumable_id;
  } else {
    rpc_request_need_timer.set_value(result, timeout);
    return cur->resumable_id;
  }
}

void f$rpc_flush() {
  update_precise_now();
  wait_net(0);
  update_precise_now();
  for (array<double>::iterator iter = rpc_request_need_timer.begin(); iter != rpc_request_need_timer.end(); ++iter) {
    int id = iter.get_key().to_int();
    rpc_request *cur = get_rpc_request(id);
    if (cur->resumable_id > 0) {
      php_assert (cur->timer == nullptr);
      cur->timer = allocate_event_timer(iter.get_value() + get_precise_now(), timeout_wakeup_id, id);
    }
  }
  rpc_request_need_timer.clear();
}

int f$rpc_send(const rpc_connection &conn, double timeout) {
  int request_id = rpc_send(conn, timeout);
  if (request_id <= 0) {
    return 0;
  }

  f$rpc_flush();
  return request_id;
}

int f$rpc_send_noflush(const rpc_connection &conn, double timeout) {
  int request_id = rpc_send(conn, timeout);
  if (request_id <= 0) {
    return 0;
  }

  return request_id;
}


void process_rpc_answer(int request_id, char *result, int result_len __attribute__((unused))) {
  rpc_request *request = get_rpc_request(request_id);

  if (request->resumable_id < 0) {
    php_assert (result != nullptr);
    dl::deallocate(result - 12, result_len + 13);
    php_assert (request->resumable_id != -1);
    return;
  }
  int resumable_id = request->resumable_id;
  request->resumable_id = -1;

  if (request->timer) {
    remove_event_timer(request->timer);
  }

  php_assert (result != nullptr);
  request->answer = result;
//  fprintf (stderr, "answer_len = %d\n", result_len);

  php_assert (resumable_id > 0);
  resumable_run_ready(resumable_id);
}

void process_rpc_error(int request_id, int error_code __attribute__((unused)), const char *error_message) {
  rpc_request *request = get_rpc_request(request_id);

  if (request->resumable_id < 0) {
    php_assert (request->resumable_id != -1);
    return;
  }
  int resumable_id = request->resumable_id;
  request->resumable_id = -2;

  if (request->timer) {
    remove_event_timer(request->timer);
  }

  request->error = error_message;

  php_assert (resumable_id > 0);
  resumable_run_ready(resumable_id);
}


class rpc_get_resumable : public Resumable {
  using ReturnT = Optional<string>;
  int resumable_id;
  double timeout;

  bool ready;
protected:
  bool run() {
    RESUMABLE_BEGIN
      ready = f$wait(resumable_id, timeout);
      TRY_WAIT(rpc_get_resumable_label_0, ready, bool);
      if (!ready) {
        last_rpc_error = last_wait_error;
        RETURN(false);
      }

      Storage *input = get_forked_storage(resumable_id);
      if (input->tag == 0) {
        last_rpc_error = "Result already was gotten";
        RETURN(false);
      }
      if (input->tag != Storage::tagger<rpc_request>::get_tag()) {
        last_rpc_error = "Not a rpc request";
        RETURN(false);
      }

      rpc_request res = input->load<rpc_request>();
      php_assert (CurException.is_null());

      if (res.resumable_id == -2) {
        last_rpc_error = res.error;
        RETURN(false);
      }

      php_assert (res.resumable_id == -1);

      string result;
      result.assign_raw(res.answer - 12);
      RETURN(result);
    RESUMABLE_END
  }

public:
  rpc_get_resumable(int resumable_id, double timeout) :
    resumable_id(resumable_id),
    timeout(timeout),
    ready(false) {
  }
};

bool drop_tl_query_info(int query_id) {
  auto query = RpcPendingQueries::get().withdraw(query_id);
  if (query.is_null()) {
    php_warning("Result of TL query with id %d has already been taken or id is incorrect", query_id);
    return false;
  }
  return true;
}

Optional<string> f$rpc_get(int request_id, double timeout) {
  if (!drop_tl_query_info(request_id)) {
    return false;
  }
  return start_resumable<Optional<string>>(new rpc_get_resumable(request_id, timeout));
}

Optional<string> f$rpc_get_synchronously(int request_id) {
  wait_synchronously(request_id);
  Optional<string> result = f$rpc_get(request_id);
  php_assert (resumable_finished);
  return result;
}

class rpc_get_and_parse_resumable : public Resumable {
  using ReturnT = bool;
  int resumable_id;
  double timeout;

  bool ready;
protected:
  bool run() {
    RESUMABLE_BEGIN
      ready = f$wait(resumable_id, timeout);
      TRY_WAIT(rpc_get_and_parse_resumable_label_0, ready, bool);
      if (!ready) {
        last_rpc_error = last_wait_error;
        RETURN(false);
      }

      Storage *input = get_forked_storage(resumable_id);
      if (input->tag == 0) {
        last_rpc_error = "Result already was gotten";
        RETURN(false);
      }
      if (input->tag != Storage::tagger<rpc_request>::get_tag()) {
        last_rpc_error = "Not a rpc request";
        RETURN(false);
      }

      rpc_request res = input->load<rpc_request>();
      php_assert (CurException.is_null());

      if (res.resumable_id == -2) {
        last_rpc_error = res.error;
        RETURN(false);
      }

      php_assert (res.resumable_id == -1);

      string result;
      result.assign_raw(res.answer - 12);
      bool parse_result = f$rpc_parse(result);
      php_assert(parse_result);

      RETURN(true);
    RESUMABLE_END
  }

public:
  rpc_get_and_parse_resumable(int resumable_id, double timeout) :
    resumable_id(resumable_id),
    timeout(timeout),
    ready(false) {
  }
};

bool f$rpc_get_and_parse(int request_id, double timeout) {
  if (!drop_tl_query_info(request_id)) {
    return false;
  }
  return rpc_get_and_parse(request_id, timeout);
}

bool rpc_get_and_parse(int request_id, double timeout) {
  return start_resumable<bool>(new rpc_get_and_parse_resumable(request_id, timeout));
}


int f$query_x2(int x) {
  return query_x2(x);
}


/*
 *
 *  var wrappers
 *
 */


bool f$store_unsigned_int(const var &v) {
  return store_unsigned_int(store_parse_number_unsigned<unsigned int>(v));
}

bool f$store_long(const var &v) {
  return store_long(store_parse_number<long long>(v));
}

bool f$store_unsigned_long(const var &v) {
  return store_unsigned_long(store_parse_number_unsigned<unsigned long long>(v));
}


/*
 *
 *     RPC_TL_QUERY
 *
 */


int tl_parse_int() {
  return TRY_CALL(int, int, (f$fetch_int()));
}

long long tl_parse_long() {
  return TRY_CALL(long long, int, (f$fetch_Long().l));
}

double tl_parse_double() {
  return TRY_CALL(double, double, (f$fetch_double()));
}

string tl_parse_string() {
  return TRY_CALL(string, string, (f$fetch_string()));
}

void tl_parse_end() {
  TRY_CALL_VOID(void, (f$fetch_end()));
}

int tl_parse_save_pos() {
  return rpc_get_pos();
}

bool tl_parse_restore_pos(int pos) {
  return rpc_set_pos(pos);
}

array<var> tl_fetch_error(const string &error, int error_code) {
  array<var> result;
  result.set_value(STR_ERROR, error);
  result.set_value(STR_ERROR_CODE, error_code);
  return result;
}

array<var> tl_fetch_error(const char *error, int error_code) {
  return tl_fetch_error(string(error, strlen(error)), error_code);
}

static long long rpc_tl_results_last_query_num = -1;

bool try_fetch_rpc_error(array<var> &out_if_error) {
  int x = rpc_lookup_int();
  if (x == TL_RPC_REQ_ERROR && CurException.is_null()) {
    php_assert (tl_parse_int() == TL_RPC_REQ_ERROR);
    if (CurException.is_null()) {
      tl_parse_long();
      if (CurException.is_null()) {
        int error_code = tl_parse_int();
        if (CurException.is_null()) {
          string error = tl_parse_string();
          if (CurException.is_null()) {
            out_if_error = tl_fetch_error(error, error_code);
            return true;
          }
        }
      }
    }
  }
  if (!CurException.is_null()) {
    out_if_error = tl_fetch_error(CurException->message, TL_ERROR_SYNTAX);
    CurException = false;
    return true;
  }
  return false;
}

class_instance<RpcQuery> store_function(const var &tl_object) {
  php_assert(CurException.is_null());
  if (!tl_object.is_array()) {
    CurrentProcessingQuery::get().raise_storing_error("Not an array passed to function rpc_tl_query");
    return {};
  }
  string fun_name = tl_arr_get(tl_object, UNDERSCORE, 0).to_string();
  if (!tl_storers_ht.has_key(fun_name)) {
    CurrentProcessingQuery::get().raise_storing_error("Function \"%s\" not found in tl-scheme", fun_name.c_str());
    return {};
  }
  class_instance<RpcQuery> rpc_query;
  rpc_query.alloc();
  rpc_query.get()->tl_function_name = fun_name;
  CurrentProcessingQuery::get().set_current_tl_function(fun_name);
  const auto &storer_kv = tl_storers_ht.get_value(fun_name);
  rpc_query.get()->result_fetcher = make_unique_on_script_memory<RpcRequestResultUntyped>(storer_kv(tl_object));
  CurrentProcessingQuery::get().reset();
  return rpc_query;
}

array<var> fetch_function(const class_instance<RpcQuery> &rpc_query) {
  array<var> new_tl_object;
  if (try_fetch_rpc_error(new_tl_object)) {
    return new_tl_object;       // тогда содержит ошибку (см. tl_fetch_error())
  }
  php_assert(!rpc_query.is_null());
  CurrentProcessingQuery::get().set_current_tl_function(rpc_query);
  auto stored_fetcher = rpc_query.get()->result_fetcher->extract_untyped_fetcher();
  php_assert(stored_fetcher);
  new_tl_object = tl_fetch_wrapper(std::move(stored_fetcher));
  CurrentProcessingQuery::get().reset();
  if (!CurException.is_null()) {
    array<var> result = tl_fetch_error(CurException->message, TL_ERROR_SYNTAX);
    CurException = false;
    return result;
  }
  if (!f$fetch_eof()) {
    php_warning("Not all data fetched");
    return tl_fetch_error("Not all data fetched", TL_ERROR_EXTRA_DATA);
  }
  return new_tl_object;
}

bool f$set_tl_mode(int mode __attribute__ ((unused))) {
  // will be deleted after not called from PHP
  return true;
}

int rpc_tl_query_impl(const rpc_connection &c, const var &tl_object, double timeout, bool ignore_answer, bool bytes_estimating, int &bytes_sent, bool flush) {
  f$rpc_clean();

  class_instance<RpcQuery> rpc_query = store_function(tl_object);
  if (!CurException.is_null()) {
    rpc_query.destroy();
    CurException = false;
  }
  if (rpc_query.is_null()) {
    return 0;
  }

  if (bytes_estimating) {
    estimate_and_flush_overflow(bytes_sent);
  }
  int query_id = rpc_send(c, timeout, ignore_answer);
  if (query_id <= 0) {
    return 0;
  }
  if (flush) {
    f$rpc_flush();
  }
  if (ignore_answer) {
    return -1;
  }
  if (dl::query_num != rpc_tl_results_last_query_num) {
    rpc_tl_results_last_query_num = dl::query_num;
  }
  rpc_query.get()->query_id = query_id;
  RpcPendingQueries::get().save(rpc_query);
  
  return query_id;
}

int f$rpc_tl_query_one(const rpc_connection &c, const var &tl_object, double timeout) {
  int bytes_sent = 0;
  return rpc_tl_query_impl(c, tl_object, timeout, false, false, bytes_sent, true);
}

int f$rpc_tl_pending_queries_count() {
  if (dl::query_num != rpc_tl_results_last_query_num) {
    return 0;
  }
  return RpcPendingQueries::get().count();
}

bool f$rpc_mc_parse_raw_wildcard_with_flags_to_array(const string &raw_result, array<var> &result) {
  if (raw_result.empty() || !f$rpc_parse(raw_result)) {
    return false;
  };

  int magic = TRY_CALL_ (int, f$fetch_int(), return false);
  if (magic != TL_DICTIONARY) {
    THROW_EXCEPTION(new_Exception(rpc_filename, __LINE__, string("Strange dictionary magic", 24), -1));
    return false;
  };

  int cnt = TRY_CALL_ (int, f$fetch_int(), return false);
  if (cnt == 0) {
    return true;
  };
  result.reserve(0, cnt + f$count(result), false);

  for (int j = 0; j < cnt; ++j) {
    string key = f$fetch_string();

    if (!CurException.is_null()) {
      return false;
    }

    var value = f$fetch_memcache_value();

    if (!CurException.is_null()) {
      return false;
    }

    result.set_value(key, value);
  };

  return true;
}

array<int> f$rpc_tl_query(const rpc_connection &c, const array<var> &tl_objects, double timeout, bool ignore_answer) {
  array<int> result(tl_objects.size());
  int bytes_sent = 0;
  for (auto it = tl_objects.begin(); it != tl_objects.end(); ++it) {
    int query_id = rpc_tl_query_impl(c, it.get_value(), timeout, ignore_answer, true, bytes_sent, false);
    result.set_value(it.get_key(), query_id);
  }
  if (bytes_sent > 0) {
    f$rpc_flush();
  }

  return result;
}


class rpc_tl_query_result_one_resumable : public Resumable {
  using ReturnT = array<var>;

  int query_id;
  class_instance<RpcQuery> rpc_query;
protected:
  bool run() {
    bool ready;

    RESUMABLE_BEGIN
      last_rpc_error = nullptr;
      ready = rpc_get_and_parse(query_id, -1);
      TRY_WAIT(rpc_get_and_parse_resumable_label_0, ready, bool);
      if (!ready) {
        php_assert (last_rpc_error != nullptr);
        if (!rpc_query.is_null()) {
          rpc_query.get()->result_fetcher.reset();
        }
        RETURN(tl_fetch_error(last_rpc_error, TL_ERROR_UNKNOWN));
      }

      array<var> tl_object = fetch_function(rpc_query);
      rpc_parse_restore_previous();
      RETURN(tl_object);
    RESUMABLE_END
  }

public:
  rpc_tl_query_result_one_resumable(int query_id, class_instance<RpcQuery> &&rpc_query) :
    query_id(query_id),
    rpc_query(std::move(rpc_query)) {
  }
};


array<var> f$rpc_tl_query_result_one(int query_id) {
  if (query_id <= 0) {
    resumable_finished = true;
    return tl_fetch_error("Wrong query_id", TL_ERROR_WRONG_QUERY_ID);
  }

  if (dl::query_num != rpc_tl_results_last_query_num) {
    resumable_finished = true;
    return tl_fetch_error("There was no TL queries in current script run", TL_ERROR_INTERNAL);
  }

  class_instance<RpcQuery> rpc_query = RpcPendingQueries::get().withdraw(query_id);
  if (rpc_query.is_null()) {
    resumable_finished = true;
    return tl_fetch_error("Can't use rpc_tl_query_result for non-TL query", TL_ERROR_INTERNAL);
  }

  return start_resumable<array<var>>(new rpc_tl_query_result_one_resumable(query_id, std::move(rpc_query)));
}


class rpc_tl_query_result_resumable : public Resumable {
  using ReturnT = array<array<var>>;

  const array<int> query_ids;
  array<array<var>> tl_objects_unsorted;
  int queue_id;
  Optional<int> query_id;

protected:
  bool run() {
    RESUMABLE_BEGIN
      if (query_ids.count() == 1) {
        query_id = query_ids.begin().get_value();

        tl_objects_unsorted[query_id] = f$rpc_tl_query_result_one(query_id.val());
        TRY_WAIT(rpc_tl_query_result_resumable_label_0, tl_objects_unsorted[query_id], array<var>);
      } else {
        queue_id = wait_queue_create(query_ids);

        while (true) {
          query_id = f$wait_queue_next(queue_id, -1);
          TRY_WAIT(rpc_tl_query_result_resumable_label_1, query_id, decltype(query_id));
          if (query_id.val() <= 0) {
            break;
          }
          tl_objects_unsorted[query_id] = f$rpc_tl_query_result_one(query_id.val());
          php_assert (resumable_finished);
        }

        unregister_wait_queue(queue_id);
      }

      array<array<var>> tl_objects(query_ids.size());
      for (array<int>::const_iterator it = query_ids.begin(); it != query_ids.end(); ++it) {
        int query_id = it.get_value();
        if (!tl_objects_unsorted.isset(query_id)) {
          if (query_id <= 0) {
            tl_objects[it.get_key()] = tl_fetch_error((static_SB.clean() << "Very wrong query_id " << query_id).str(), TL_ERROR_WRONG_QUERY_ID);
          } else {
            tl_objects[it.get_key()] = tl_fetch_error((static_SB.clean() << "No answer received or duplicate/wrong query_id "
                                                                         << query_id).str(), TL_ERROR_WRONG_QUERY_ID);
          }
        } else {
          tl_objects[it.get_key()] = tl_objects_unsorted[query_id];
        }
      }

      RETURN(tl_objects);
    RESUMABLE_END
  }

public:
  rpc_tl_query_result_resumable(const array<int> &query_ids) :
    query_ids(query_ids),
    tl_objects_unsorted(array_size(query_ids.count(), 0, false)),
    queue_id(0),
    query_id(0) {
  }
};

array<array<var>> f$rpc_tl_query_result(const array<int> &query_ids) {
  return start_resumable<array<array<var>>>(new rpc_tl_query_result_resumable(query_ids));
}

array<array<var>> f$rpc_tl_query_result_synchronously(const array<int> &query_ids) {
  array<array<var>> tl_objects_unsorted(array_size(query_ids.count(), 0, false));
  if (query_ids.count() == 1) {
    f$wait_synchronously(query_ids.begin().get_value());
    tl_objects_unsorted[query_ids.begin().get_value()] = f$rpc_tl_query_result_one(query_ids.begin().get_value());
    php_assert (resumable_finished);
  } else {
    int queue_id = wait_queue_create(query_ids);

    while (true) {
      int query_id = f$wait_queue_next_synchronously(queue_id).val();
      if (query_id <= 0) {
        break;
      }
      tl_objects_unsorted[query_id] = f$rpc_tl_query_result_one(query_id);
      php_assert (resumable_finished);
    }

    unregister_wait_queue(queue_id);
  }

  array<array<var>> tl_objects(query_ids.size());
  for (array<int>::const_iterator it = query_ids.begin(); it != query_ids.end(); ++it) {
    int query_id = it.get_value();
    if (!tl_objects_unsorted.isset(query_id)) {
      if (query_id <= 0) {
        tl_objects[it.get_key()] = tl_fetch_error((static_SB.clean() << "Very wrong query_id " << query_id).str(), TL_ERROR_WRONG_QUERY_ID);
      } else {
        tl_objects[it.get_key()] = tl_fetch_error((static_SB.clean() << "No answer received or duplicate/wrong query_id "
                                                                     << query_id).str(), TL_ERROR_WRONG_QUERY_ID);
      }
    } else {
      tl_objects[it.get_key()] = tl_objects_unsorted[query_id];
    }
  }

  return tl_objects;
}

void global_init_rpc_lib() {
  php_assert (timeout_wakeup_id == -1);

  timeout_wakeup_id = register_wakeup_callback(&process_rpc_timeout);
}

static void reset_rpc_global_vars() {
  hard_reset_var(rpc_filename);
  hard_reset_var(rpc_data_copy);
  hard_reset_var(rpc_data_copy_backup);
  hard_reset_var(rpc_request_need_timer);
}

void init_rpc_lib() {
  php_assert (timeout_wakeup_id != -1);

  CurrentProcessingQuery::get().reset();
  RpcPendingQueries::get().hard_reset();
  reset_rpc_global_vars();

  rpc_parse(nullptr, 0);
  // init backup
  rpc_parse(nullptr, 0);

  f$rpc_clean(false);
  rpc_stored = 0;

  rpc_pack_threshold = -1;
  rpc_pack_from = -1;
  rpc_filename = string("rpc.cpp", 7);
}

void free_rpc_lib() {
  reset_rpc_global_vars();
  RpcPendingQueries::get().hard_reset();
  CurrentProcessingQuery::get().reset();
}

int f$rpc_queue_create() {
  return f$wait_queue_create();
}

int f$rpc_queue_create(const var &request_ids) {
  return f$wait_queue_create(request_ids);
}

int f$rpc_queue_push(int queue_id, const var &request_ids) {
  return f$wait_queue_push(queue_id, request_ids);
}

bool f$rpc_queue_empty(int queue_id) {
  return f$wait_queue_empty(queue_id);
}

Optional<int> f$rpc_queue_next(int queue_id, double timeout) {
  return f$wait_queue_next(queue_id, timeout);
}

Optional<int> f$rpc_queue_next_synchronously(int queue_id) {
  return f$wait_queue_next_synchronously(queue_id);
}

bool f$rpc_wait(int request_id) {
  return f$wait(request_id);
}

bool f$rpc_wait_multiple(int request_id) {
  return f$wait_multiple(request_id);
}
