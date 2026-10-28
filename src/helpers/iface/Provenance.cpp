#include "Provenance.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "IfaceEnvelope.h"

namespace Iface {

namespace {

const char DOMAIN[] = "MECON-UPLINK-v1";

}  // namespace

void base64Encode(char* out, const uint8_t* in, size_t n) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = T[v >> 18 & 63];
    out[o++] = T[v >> 12 & 63];
    out[o++] = i + 1 < n ? T[v >> 6 & 63] : '=';
    out[o++] = i + 2 < n ? T[v & 63] : '=';
  }
  out[o] = 0;
}


int Provenance::sign(const char* stream, char* buf, size_t len, size_t cap, const mesh::LocalIdentity* signer,
                     const char* extra) {
  if (!_auth || !_auth->enrolled()) return (int)len;   // a standalone device signs nothing
  if (!_id || len < 2 || buf[len - 1] != '}' || len >= cap) return -1;
  const mesh::LocalIdentity* key = signer ? signer : _id;
  char tail[160];
  int tl;
  if (!_epoch_saved || _seq == 0xFFFFFFFFu) {
    // The boot counter may repeat (or this boot's own is spent): sign nothing, and say so.
    tl = snprintf(tail, sizeof(tail), ",\"unsigned\":\"%s\"}", _epoch_saved ? "seq_exhausted" : "epoch_unsaved");
    if (tl < 0 || len - 1 + (size_t)tl >= cap) return -1;
    memcpy(buf + len - 1, tail, (size_t)tl + 1);
    return (int)(len - 1 + (size_t)tl);
  }
  const uint32_t seq = ++_seq;
  tl = snprintf(tail, sizeof(tail), "%s,\"usig_boot\":%lu,\"usig_seq\":%lu}", extra ? extra : "", (unsigned long)_boot,
                (unsigned long)seq);
  if (tl < 0 || (size_t)tl >= sizeof(tail)) return -1;
  const size_t text_len = len - 1 + (size_t)tl;
  static const size_t SIG_MEMBER = 9 + 88 + 2;   // ,"usig":"  + 88 base64 characters + "}   (and the NUL, kept inside cap)
  if (text_len + SIG_MEMBER >= cap) return -1;
  const size_t slen = strlen(stream);
  const size_t pre = sizeof(DOMAIN) /* with its NUL */ + slen + 1;
  uint8_t* msg = static_cast<uint8_t*>(malloc(pre + text_len));
  if (!msg) return -1;
  memcpy(msg, DOMAIN, sizeof(DOMAIN));
  memcpy(msg + sizeof(DOMAIN), stream, slen + 1);
  memcpy(msg + pre, buf, len - 1);
  memcpy(msg + pre + len - 1, tail, (size_t)tl);
  uint8_t sig[64];
  key->sign(sig, msg, (int)(pre + text_len));
  free(msg);
  memcpy(buf + len - 1, tail, (size_t)tl - 1);   // the tail without its closing brace
  size_t n = text_len - 1;
  memcpy(buf + n, ",\"usig\":\"", 9);
  n += 9;
  base64Encode(buf + n, sig, 64);
  n += 88;
  buf[n++] = '"';
  buf[n++] = '}';
  buf[n] = 0;
  return (int)n;
}

bool Provenance::allowRefusalSignature() {
  const uint32_t now = millis();
  if (!_tok_init) { _tok_init = true; _tok_ms = now; }
  const uint32_t dt = now - _tok_ms;
  _tok_ms = now;
  const uint32_t add = dt > 4000 ? RATE_BURST * 1000 : dt * RATE_PER_S;   // dt ms at RATE_PER_S a second, in thousandths
  _tokens = _tokens + add > RATE_BURST * 1000 ? RATE_BURST * 1000 : _tokens + add;
  if (_tokens < 1000) return false;
  _tokens -= 1000;
  return true;
}

namespace {
// Whether the result in buf is a refusal: the first "status" member is the envelope's own (the id before it is escaped, so
// it cannot contain a raw quote).
bool isRefusal(const char* buf, size_t len) {
  static const char K[] = "\"status\":\"refused\"";
  const size_t kl = sizeof(K) - 1;
  for (size_t i = 0; i + kl <= len; i++) {
    if (buf[i] == '"' && memcmp(buf + i, "\"status\":\"", 10) == 0) return memcmp(buf + i, K, kl) == 0;
  }
  return false;
}
}  // namespace

int Provenance::signResult(char* buf, size_t len, size_t cap, const mesh::LocalIdentity* signer, const Answering* a) {
  if (!_auth || !_auth->enrolled()) return (int)len;   // a standalone device signs nothing
  if (len >= 2 && isRefusal(buf, len) && !allowRefusalSignature()) {
    static const char T[] = ",\"unsigned\":\"rate\"}";
    if (len - 1 + sizeof(T) - 1 >= cap) return -1;
    memcpy(buf + len - 1, T, sizeof(T));
    return (int)(len - 1 + sizeof(T) - 1);
  }
  char extra[72];
  extra[0] = 0;
  if (a && a->op[0]) {
    const int n = a->has_job ? snprintf(extra, sizeof(extra), ",\"op\":\"%s\",\"job_id\":%lu", a->op, (unsigned long)a->job)
                             : snprintf(extra, sizeof(extra), ",\"op\":\"%s\"", a->op);
    if (n < 0 || (size_t)n >= sizeof(extra)) extra[0] = 0;
  }
  int n = sign("result", buf, len, cap, signer, extra);
  if (n >= 0) return n;
  n = writeResult(buf, cap, "", ST_BUSY, nullptr, "too-large-to-sign");
  if (n < 0) return -1;
  return sign("result", buf, (size_t)n, cap, signer, extra);
}

}  // namespace Iface
