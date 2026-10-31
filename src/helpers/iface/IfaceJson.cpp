#define IFACE_JSMN_IMPL
#include "IfaceJson.h"
#include <ctype.h>
#include <string.h>

namespace Iface {

bool charsetOk(Charset cs, char c) {
  uint8_t u = (uint8_t)c;
  switch (cs) {
    case CS_JOB:  if (u == '#') return true;   // and the CS_ID set
    // fall through
    case CS_ID:   return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '.' || u == '_' || u == ':' || u == '-';
    case CS_NAME: return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '.' || u == '_' || u == '-';
    default:      return u >= 0x20 && u != 0x7F;
  }
}

static int skipTok(const JsonDoc& d, int t) {
  int n = d.tok[t].size;
  int i = t + 1;
  if (d.tok[t].type == JSMN_OBJECT) n *= 2;
  else if (d.tok[t].type != JSMN_ARRAY) return i;
  while (n-- > 0) i = skipTok(d, i);
  return i;
}

static bool keyEq(const JsonDoc& d, int t, int u) {
  return d.tlen(t) == d.tlen(u) && memcmp(d.str(t), d.str(u), d.tlen(t)) == 0;
}

// Strict RFC 8259 grammar check. jsmn is deliberately lenient (trailing commas, missing commas, "tru" as a
// primitive), and the interface must reject what a conforming client would never send.
namespace {
struct Grammar {
  const char* s; size_t n, i; JsonErr err;
  bool fail(JsonErr e) { if (err == J_OK) err = e; return false; }
  void ws() { while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }
  bool lit(const char* w) { size_t k = strlen(w); if (n - i < k || memcmp(s + i, w, k) != 0) return fail(J_SYNTAX); i += k; return true; }
  bool digits() { size_t st = i; while (i < n && s[i] >= '0' && s[i] <= '9') i++; return i > st ? true : fail(J_SYNTAX); }
  bool number() {
    if (i < n && s[i] == '-') i++;
    if (i < n && s[i] == '0') i++;
    else if (i < n && s[i] >= '1' && s[i] <= '9') digits();
    else return fail(J_SYNTAX);
    if (i < n && s[i] == '.') { i++; if (!digits()) return false; }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) { i++; if (i < n && (s[i] == '+' || s[i] == '-')) i++; if (!digits()) return false; }
    return true;
  }
  bool string() {
    i++;   // opening quote
    while (i < n) {
      uint8_t c = (uint8_t)s[i++];
      if (c == '"') return true;
      if (c < 0x20) return fail(J_CTRL);
      if (c == '\\') {
        if (i >= n) break;
        char e = s[i++];
        if (e == 'u') { for (int k = 0; k < 4; k++, i++) if (i >= n || !isxdigit((uint8_t)s[i])) return fail(J_SYNTAX); }
        else if (!strchr("\"\\/bfnrt", e) || e == 0) return fail(J_SYNTAX);
      }
    }
    return fail(J_SYNTAX);
  }
  bool value(int depth) {
    ws();
    if (i >= n) return fail(J_SYNTAX);
    char c = s[i];
    if (c == '{' || c == '[') {
      if (depth >= MAX_DEPTH) return fail(J_TOO_DEEP);
      char close = c == '{' ? '}' : ']';
      i++; ws();
      if (i < n && s[i] == close) { i++; return true; }
      for (;;) {
        ws();
        if (c == '{') {
          if (i >= n || s[i] != '"' || !string()) return fail(J_SYNTAX);
          ws();
          if (i >= n || s[i] != ':') return fail(J_SYNTAX);
          i++;
        }
        if (!value(depth + 1)) return false;
        ws();
        if (i < n && s[i] == ',') { i++; continue; }
        if (i < n && s[i] == close) { i++; return true; }
        return fail(J_SYNTAX);
      }
    }
    if (c == '"') return string();
    if (c == 't') return lit("true");
    if (c == 'f') return lit("false");
    if (c == 'n') return lit("null");
    return number();
  }
};
}  // namespace

JsonErr jsonParse(JsonDoc& d, const char* s, size_t len) {
  d.s = s; d.len = len; d.ntok = 0;
  if (!s || len == 0 || len > MAX_MSG) return J_TOO_LONG;
  Grammar g = { s, len, 0, J_OK };
  if (!g.value(0)) return g.err;
  g.ws();
  if (g.i != len) return J_TRAILING;    // a second value, or a NUL, after the root
  jsmn_parser p;
  jsmn_init(&p);
  int n = jsmn_parse(&p, s, len, d.tok, d.cap);
  if (n == JSMN_ERROR_NOMEM) return J_TOO_MANY_TOKENS;
  if (n < 1) return J_SYNTAX;
  d.ntok = n;
  if (d.tok[0].type != JSMN_OBJECT) return J_NOT_OBJECT;
  // jsmn happily goes on to a second root value and stops at a NUL: the one root has to span the whole input
  size_t end = len;
  while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r')) end--;
  if ((size_t)d.tok[0].end != end || n != skipTok(d, 0)) return J_TRAILING;

  int stack[MAX_DEPTH + 1];
  int sp = 0;
  for (int i = 0; i < n; i++) {
    while (sp > 0 && stack[sp - 1] <= d.tok[i].start) sp--;
    if (d.tok[i].type == JSMN_OBJECT || d.tok[i].type == JSMN_ARRAY) {
      if (sp >= MAX_DEPTH) return J_TOO_DEEP;
      stack[sp++] = d.tok[i].end;
    } else if (d.tok[i].type == JSMN_STRING) {
      for (int k = d.tok[i].start; k < d.tok[i].end; k++)
        if ((uint8_t)s[k] < 0x20) return J_CTRL;
    }
  }
  for (int i = 0; i < n; i++) {
    if (d.tok[i].type != JSMN_OBJECT) continue;
    int keys[MAX_TOKENS_LARGE / 2];
    int nk = 0, c = i + 1;
    for (int k = 0; k < d.tok[i].size; k++) {
      for (int j = 0; j < nk; j++)
        if (keyEq(d, keys[j], c)) return J_DUP_KEY;
      keys[nk++] = c;
      c = skipTok(d, c + 1);
    }
  }
  return J_OK;
}

int jsonSkip(const JsonDoc& d, int t) { return skipTok(d, t); }

int jsonFind(const JsonDoc& d, int obj, const char* key) {
  if (!isObject(d, obj)) return -1;
  size_t kl = strlen(key);
  int c = obj + 1;
  for (int k = 0; k < d.tok[obj].size; k++) {
    if (d.tok[c].type == JSMN_STRING && (size_t)d.tlen(c) == kl && memcmp(d.str(c), key, kl) == 0) return c + 1;
    c = skipTok(d, c + 1);
  }
  return -1;
}

int jsonElem(const JsonDoc& d, int arr, int n) {
  if (!isArray(d, arr) || n < 0 || n >= d.tok[arr].size) return -1;
  int c = arr + 1;
  while (n-- > 0) c = skipTok(d, c);
  return c;
}

bool jsonGetString(const JsonDoc& d, int t, char* out, size_t cap, Charset cs, size_t minLen) {
  if (t < 0 || d.tok[t].type != JSMN_STRING || cap == 0) return false;
  size_t n = 0;
  const char* p = d.str(t);
  const char* e = p + d.tlen(t);
  while (p < e) {
    char c = *p++;
    if (c == '\\') {
      if (p >= e) return false;
      switch (*p++) {
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case '/': c = '/'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        default: return false;   // \u included
      }
      // a decoded control character is as unwelcome as a raw one, except where the caller allows it (none do)
    }
    if (n + 1 >= cap || !charsetOk(cs, c)) return false;
    out[n++] = c;
  }
  if (n < minLen) return false;
  out[n] = 0;
  return true;
}

bool jsonGetInt(const JsonDoc& d, int t, int64_t lo, int64_t hi, int64_t* out) {
  if (t < 0 || d.tok[t].type != JSMN_PRIMITIVE) return false;
  const char* p = d.str(t);
  int n = d.tlen(t);
  bool neg = false;
  if (n > 0 && p[0] == '-') { neg = true; p++; n--; }
  if (n < 1 || n > 18) return false;
  if (p[0] == '0' && (n > 1 || neg)) return false;
  int64_t v = 0;
  for (int i = 0; i < n; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    v = v * 10 + (p[i] - '0');
  }
  if (neg) v = -v;
  if (v < lo || v > hi) return false;
  *out = v;
  return true;
}

bool jsonGetBool(const JsonDoc& d, int t, bool* out) {
  if (t < 0 || d.tok[t].type != JSMN_PRIMITIVE) return false;
  if (d.tlen(t) == 4 && memcmp(d.str(t), "true", 4) == 0) { *out = true; return true; }
  if (d.tlen(t) == 5 && memcmp(d.str(t), "false", 5) == 0) { *out = false; return true; }
  return false;
}

bool jsonIsNull(const JsonDoc& d, int t) {
  return t >= 0 && d.tok[t].type == JSMN_PRIMITIVE && d.tlen(t) == 4 && memcmp(d.str(t), "null", 4) == 0;
}

// ---- writer

void JsonWriter::put(char c) {
  if (_fail) return;
  if (_n + 1 >= _cap) { _fail = true; return; }   // keep room for the terminator
  _b[_n++] = c;
  _b[_n] = 0;
}

void JsonWriter::value() {
  if (_after_key) { _after_key = false; return; }
  if (_depth > 0) {
    if (_need_comma[_depth - 1]) put(',');
    _need_comma[_depth - 1] = true;
  }
}

void JsonWriter::open(char c) {
  value();
  if (_depth >= MAX_NEST) { _fail = true; return; }
  put(c);
  _need_comma[_depth++] = false;
}

void JsonWriter::close(char c) {
  if (_depth == 0) { _fail = true; return; }
  _depth--;
  put(c);
}

void JsonWriter::key(const char* k) {
  value();
  string(k, strlen(k));
  put(':');
  _after_key = true;
}

// Length of the valid UTF-8 sequence at v (1 to 4 bytes), or 0 when it is not one: a stray or missing continuation byte,
// an overlong form, a surrogate and a code point over U+10FFFF are all invalid.
static size_t utf8Len(const uint8_t* v, size_t n) {
  const uint8_t c = v[0];
  if (c < 0x80) return 1;
  size_t k;
  uint32_t cp;
  if (c >= 0xC2 && c <= 0xDF) { k = 2; cp = c & 0x1F; }
  else if (c >= 0xE0 && c <= 0xEF) { k = 3; cp = c & 0x0F; }
  else if (c >= 0xF0 && c <= 0xF4) { k = 4; cp = c & 0x07; }
  else return 0;
  if (n < k) return 0;
  for (size_t j = 1; j < k; j++) {
    if ((v[j] & 0xC0) != 0x80) return 0;
    cp = cp << 6 | (v[j] & 0x3F);
  }
  if (k == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
  if (k == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
  return k;
}

// Text from the air (a contact's advertised name, cut at a byte limit) need not be UTF-8: each byte that does not start
// a valid sequence is written as U+FFFD, so the message stays valid JSON for a strict decoder.
void JsonWriter::string(const char* v, size_t n) {
  put('"');
  static const char hex[] = "0123456789abcdef";
  const uint8_t* b = reinterpret_cast<const uint8_t*>(v);
  for (size_t i = 0; i < n; i++) {
    uint8_t c = b[i];
    if (c == '"' || c == '\\') { put('\\'); put((char)c); }
    else if (c < 0x20) { put('\\'); put('u'); put('0'); put('0'); put(hex[c >> 4]); put(hex[c & 15]); }
    else if (c < 0x80) put((char)c);
    else {
      const size_t k = utf8Len(b + i, n - i);
      if (!k) { put('\\'); put('u'); put('f'); put('f'); put('f'); put('d'); continue; }
      for (size_t j = 0; j < k; j++) put((char)b[i + j]);
      i += k - 1;
    }
  }
  put('"');
}

void JsonWriter::digits(uint64_t v) {
  char t[24];
  int n = 0;
  do { t[n++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
  while (n) put(t[--n]);
}

void JsonWriter::num(int64_t v) {
  value();
  if (v < 0) { put('-'); digits((uint64_t)0 - (uint64_t)v); }
  else digits((uint64_t)v);
}

void JsonWriter::unum(uint64_t v) {
  value();
  digits(v);
}

void JsonWriter::raw(const char* json, size_t n) {
  value();
  for (size_t i = 0; i < n; i++) put(json[i]);
}

}  // namespace Iface
