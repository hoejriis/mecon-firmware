#pragma once
// Bounded JSON for the device interface: a jsmn parse into a fixed token array (no heap), typed field
// readers, and a bounds-checked writer. docs/contract/DEVICE_INTERFACE.md section 3.
#include <stddef.h>
#include <stdint.h>

#define JSMN_STRICT
#ifndef IFACE_JSMN_IMPL
#define JSMN_HEADER   // only IfaceJson.cpp compiles jsmn's functions, so there is exactly one copy
#endif
#include "jsmn.h"

namespace Iface {

const size_t MAX_MSG = 4096;     // bytes per message after reassembly
const int MAX_TOKENS = 96;
const int MAX_DEPTH = 4;         // containers nested, the root object counting as 1

enum JsonErr {
  J_OK = 0,
  J_TOO_LONG,        // input over MAX_MSG, or empty
  J_SYNTAX,
  J_TOO_MANY_TOKENS,
  J_TOO_DEEP,
  J_TRAILING,        // anything after the one root value, a NUL included
  J_NOT_OBJECT,
  J_DUP_KEY,         // the same key twice in one object
  J_CTRL             // a raw control character inside a string
};

enum Charset {
  CS_ANY,   // any decoded byte >= 0x20 except 0x7F
  CS_ID,    // [0-9A-Za-z._:-]  request ids
  CS_NAME   // [0-9A-Za-z._-]   operation names and keys
};

// About 1.6 KB: lives on the caller's stack for the duration of one request, never in a static.
struct JsonDoc {
  const char* s;
  size_t len;
  jsmntok_t tok[MAX_TOKENS];
  int ntok;
  const char* str(int t) const { return s + tok[t].start; }
  int tlen(int t) const { return tok[t].end - tok[t].start; }
};

// Parses one JSON object (the only root the interface accepts). The input is not copied and must outlive doc.
JsonErr jsonParse(JsonDoc& doc, const char* s, size_t len);

inline bool isObject(const JsonDoc& d, int t) { return t >= 0 && d.tok[t].type == JSMN_OBJECT; }
inline bool isArray(const JsonDoc& d, int t) { return t >= 0 && d.tok[t].type == JSMN_ARRAY; }

// Token index of the value stored under key in object token obj, or -1.
int jsonFind(const JsonDoc& d, int obj, const char* key);
// Token index of the element n of array token arr, or -1.
int jsonElem(const JsonDoc& d, int arr, int n);

// Decodes a string token into out (NUL terminated). False when it is not a string, does not fit cap with
// its terminator, is shorter than minLen, has a byte outside cs, or uses \u (not supported on purpose).
bool jsonGetString(const JsonDoc& d, int t, char* out, size_t cap, Charset cs, size_t minLen = 0);
// Plain JSON integer (no fraction, exponent, leading zero or "-0") within [lo, hi].
bool jsonGetInt(const JsonDoc& d, int t, int64_t lo, int64_t hi, int64_t* out);
bool jsonGetBool(const JsonDoc& d, int t, bool* out);
bool jsonIsNull(const JsonDoc& d, int t);

bool charsetOk(Charset cs, char c);

// Appends JSON to a caller buffer. Every write is bounds checked; after the first overflow or misuse the
// writer stays failed and ok() is false, so a result is either complete or visibly not.
class JsonWriter {
public:
  JsonWriter(char* buf, size_t cap) : _b(buf), _cap(cap), _n(0), _depth(0), _fail(cap == 0) { if (cap) _b[0] = 0; }
  void beginObject() { open('{'); }
  void endObject() { close('}'); }
  void beginArray() { open('['); }
  void endArray() { close(']'); }
  void key(const char* k);                 // inside an object; the next call writes its value
  void str(const char* v) { value(); string(v, v ? strlen_(v) : 0); }
  void str(const char* v, size_t n) { value(); string(v, n); }
  void num(int64_t v);
  void unum(uint64_t v);
  void boolean(bool v) { value(); raw_(v ? "true" : "false"); }
  void null() { value(); raw_("null"); }
  void raw(const char* json, size_t n);    // a value that is already JSON; the caller vouches for it
  void raw(const char* json) { raw(json, strlen_(json)); }
  bool ok() const { return !_fail; }
  size_t size() const { return _n; }
  void reset() { _n = 0; _depth = 0; _fail = (_cap == 0); if (_cap) _b[0] = 0; }
  bool balanced() const { return _depth == 0; }
private:
  static const int MAX_NEST = 8;
  static size_t strlen_(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
  void open(char c);
  void close(char c);
  void value();                            // writes the separating comma when one is due
  void put(char c);
  void digits(uint64_t v);
  void raw_(const char* s) { while (*s) put(*s++); }
  void string(const char* v, size_t n);
  char* _b; size_t _cap, _n;
  int _depth;
  bool _fail;
  bool _need_comma[MAX_NEST];
  bool _after_key = false;
};

}  // namespace Iface
