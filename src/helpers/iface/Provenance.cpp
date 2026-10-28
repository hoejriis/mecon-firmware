#include "Provenance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "IfaceEnvelope.h"

namespace Iface {

namespace {

const char DOMAIN[] = "MECON-UPLINK-v1";

void b64(char* out, const uint8_t* in, size_t n) {
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

}  // namespace

int Provenance::sign(const char* stream, char* buf, size_t len, size_t cap, const mesh::LocalIdentity* signer) {
  if (!_auth || !_auth->enrolled()) return (int)len;   // a standalone device signs nothing
  if (!_id || len < 2 || buf[len - 1] != '}' || len >= cap) return -1;
  const mesh::LocalIdentity* key = signer ? signer : _id;
  char tail[64];
  int tl;
  if (!_epoch_saved || _seq == 0xFFFFFFFFu) {
    // The boot counter may repeat (or this boot's own is spent): sign nothing, and say so.
    tl = snprintf(tail, sizeof(tail), ",\"unsigned\":\"%s\"}", _epoch_saved ? "seq_exhausted" : "epoch_unsaved");
    if (tl < 0 || len - 1 + (size_t)tl >= cap) return -1;
    memcpy(buf + len - 1, tail, (size_t)tl + 1);
    return (int)(len - 1 + (size_t)tl);
  }
  const uint32_t seq = ++_seq;
  tl = snprintf(tail, sizeof(tail), ",\"usig_boot\":%lu,\"usig_seq\":%lu}", (unsigned long)_boot, (unsigned long)seq);
  if (tl < 0) return -1;
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
  b64(buf + n, sig, 64);
  n += 88;
  buf[n++] = '"';
  buf[n++] = '}';
  buf[n] = 0;
  return (int)n;
}

int Provenance::signResult(char* buf, size_t len, size_t cap, const mesh::LocalIdentity* signer) {
  int n = sign("result", buf, len, cap, signer);
  if (n >= 0) return n;
  n = writeResult(buf, cap, "", ST_BUSY, nullptr, "too-large-to-sign");
  if (n < 0) return -1;
  return sign("result", buf, (size_t)n, cap, signer);
}

}  // namespace Iface
