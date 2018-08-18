#pragma once

#include "compiler/common.h"

inline string get_full_path(const string &file_name) {
  char name[PATH_MAX + 1];
  char *ptr = realpath(file_name.c_str(), name);

  if (ptr == NULL) {
    return "";
  } else {
    return name;
  }
}

static inline int is_alpha(int c);
static inline int is_alphanum(int c);
static inline int is_digit(int c);
static inline int conv_oct_digit(int c);
static inline int conv_hex_digit(int c);

template<class T>
void clear(T &val);

template<class T>
void save_to_ptr(T *ptr, const T &data);

template<class ArrayType, class IndexType>
IndexType dsu_get(const ArrayType &arr, IndexType i);

template<class ArrayType, class IndexType>
void dsu_uni(ArrayType *arr, IndexType i, IndexType j);

template<class T>
class Enumerator {
public:
  vector<T> vars;

  int size(void) {
    return vars.size();
  }

  Enumerator() {
  }

  int next_id(const T &new_data) {
    vars.push_back(new_data);
    return (int)vars.size();
  }

  T &operator[](int id) {
    assert (0 < id && id <= (int)vars.size());
    return vars[id - 1];
  }

private:
  DISALLOW_COPY_AND_ASSIGN (Enumerator);
};

template<typename KeyT, typename EntryT>
class MapToId {
public:
  explicit MapToId(Enumerator<EntryT> *cur_id);

  int get_id(const KeyT &name);
  int add_name(const KeyT &name, const EntryT &add);
  EntryT &operator[](int id);

  set<int> get_ids();

private:
  map<KeyT, int> name_to_id;
  Enumerator<EntryT> *items;
  DISALLOW_COPY_AND_ASSIGN (MapToId);
};

class string_ref {
private:
  const char *s, *t;

public:
  string_ref();
  string_ref(const char *s, const char *t);

  int length() const;

  const char *begin() const;
  const char *end() const;

  string str() const;
  const char *c_str() const;

  bool starts_with(const char *rhs) const;

  inline operator string() const {
    return string(s, t);
  }
};

inline string_ref::string_ref()
  : s(NULL), t(NULL) {}

inline string_ref::string_ref(const char *s, const char *t)
  : s(s), t(t) {}

inline int string_ref::length() const {
  return (int)(t - s);
}

inline const char *string_ref::begin() const {
  return s;
}

inline const char *string_ref::end() const {
  return t;
}

inline bool operator==(const string_ref &lhs, const char *rhs) {
  int len = (int)strlen(rhs);
  return len == lhs.length() && strncmp(rhs, lhs.begin(), len) == 0;
}

inline string_ref string_ref_dup(const string &s) {
  char *buf = new char[s.length()];
  memcpy(buf, &s[0], s.size());
  return string_ref(buf, buf + s.length());
}

inline string string_ref::str() const {
  return string(begin(), end());
}

inline const char *string_ref::c_str() const {
  return str().c_str();
}

inline bool string_ref::starts_with(const char *str) const {
  int len = (int)strlen(str);
  return len <= length() && strncmp(str, begin(), len) == 0;
}

inline string int_to_str(int x) {
  char tmp[50];
  sprintf(tmp, "%d", x);
  return tmp;
}

inline vector<string> split(const string &s, char delimiter = ' ') {
  vector <string> res;

  int prev = 0;
  for (int i = 0; i <= (int)s.size(); i++) {
    if (s[i] == delimiter || s[i] == 0) {
      if (prev != i) {
        res.push_back (s.substr (prev, i - prev));
      }
      prev = i + 1;
    }
  }

  return res;
}

template<class T>
void my_unique(T *v);

#include "utils.hpp"
#include "graph.h"
