#pragma once

#include "runtime/kphp_core.h"

const int ZLIB_COMPRESS = 0x0f;
const int ZLIB_ENCODE = 0x1f;

const string_buffer *zlib_encode (const char *s, int s_len, int level, int encoding);//returns pointer to static_SB

string f$gzcompress (const string &s, int level = -1);

string f$gzuncompress (const string &s);

string f$gzencode (const string &s, int level = -1);

string f$gzdecode (const string &s);
