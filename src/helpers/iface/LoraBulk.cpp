#include "LoraBulk.h"
#include "helpers/updates/UpdFeatures.h"

#if defined(UPD_LORA)   // only where a firmware update over LoRa is carried (UpdFeatures.h)
#include <string.h>
#include "../deployment/DepHmac.h"

namespace Iface {

namespace {

const char BULK_LABEL[] = "mecon lora bulk v1";
const char ANSWER_LABEL[] = "mecon lora answer v1";

void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The 8-byte tag over everything before it, under one direction's key.
void tag(const uint8_t key[32], const uint8_t* msg, size_t len, uint8_t out[BULK_TAG]) {
  uint8_t mac[Dep::HMAC_LEN];
  Dep::hmacSha256(key, 32, msg, len, mac);
  memcpy(out, mac, BULK_TAG);
  memset(mac, 0, sizeof(mac));
}

bool tagOk(const uint8_t key[32], const uint8_t* msg, size_t len, const uint8_t* got) {
  uint8_t want[BULK_TAG];
  tag(key, msg, len, want);
  const bool ok = Dep::constTimeEqual(want, got, BULK_TAG);
  memset(want, 0, sizeof(want));
  return ok;
}

}  // namespace

void bulkKeys(const uint8_t* token, size_t token_len, BulkKeys* out) {
  Dep::hmacSha256(token, token_len, (const uint8_t*)BULK_LABEL, sizeof(BULK_LABEL) - 1, out->bulk);
  Dep::hmacSha256(token, token_len, (const uint8_t*)ANSWER_LABEL, sizeof(ANSWER_LABEL) - 1, out->answer);
}

void bulkKeysWipe(BulkKeys* k) {
  volatile uint8_t* p = (volatile uint8_t*)k;
  for (size_t i = 0; i < sizeof(*k); i++) p[i] = 0;
}

uint32_t bulkFramesTotal(uint32_t transfer_size) {
  return (uint32_t)((transfer_size + (uint64_t)BULK_DATA - 1) / BULK_DATA);
}

size_t bulkFrameDataLen(uint32_t transfer_size, uint32_t index) {
  const uint32_t total = bulkFramesTotal(transfer_size);
  if (index >= total) return 0;
  if (index + 1 < total) return BULK_DATA;
  return (size_t)(transfer_size - (uint64_t)index * BULK_DATA);   // the last frame: exactly what is left
}

size_t bulkFrameEncode(const BulkKeys& k, uint32_t job, uint32_t transfer_size, uint32_t index, const uint8_t* data,
                       size_t n, uint8_t* out, size_t cap) {
  const size_t want = bulkFrameDataLen(transfer_size, index);
  if (want == 0 || n != want || cap < BULK_HDR + n + BULK_TAG) return 0;
  out[0] = BULK_MARKER;
  put32(out + 1, job);
  put32(out + 5, index);
  memcpy(out + BULK_HDR, data, n);
  tag(k.bulk, out, BULK_HDR + n, out + BULK_HDR + n);
  return BULK_HDR + n + BULK_TAG;
}

const char* bulkCheckName(BulkCheck c) {
  switch (c) {
    case BULK_OK: return "ok";
    case BULK_NOT_BULK: return "not-bulk";
    case BULK_OTHER_JOB: return "other-job";
    case BULK_PAST_END: return "past-end";
    case BULK_BAD_LEN: return "bad-length";
    case BULK_BAD_TAG: return "bad-tag";
  }
  return "?";
}

BulkCheck bulkFrameDecode(const BulkKeys& k, uint32_t job, uint32_t transfer_size, const uint8_t* f, size_t n,
                          uint32_t* index, const uint8_t** data, size_t* data_len) {
  if (n < BULK_HDR + 1 + BULK_TAG || f[0] != BULK_MARKER) return BULK_NOT_BULK;
  if (get32(f + 1) != job) return BULK_OTHER_JOB;
  const uint32_t idx = get32(f + 5);
  const size_t want = bulkFrameDataLen(transfer_size, idx);
  if (want == 0) return BULK_PAST_END;
  if (n != BULK_HDR + want + BULK_TAG) return BULK_BAD_LEN;
  if (!tagOk(k.bulk, f, BULK_HDR + want, f + BULK_HDR + want)) return BULK_BAD_TAG;
  *index = idx;
  *data = f + BULK_HDR;
  *data_len = want;
  return BULK_OK;
}

void answerEncode(const BulkKeys& k, uint32_t job, uint32_t base, uint32_t next, uint8_t have, uint8_t out[ANSWER_LEN]) {
  out[0] = ANSWER_MARKER;
  put32(out + 1, job);
  put32(out + 5, base);
  put32(out + 9, next);
  out[13] = have;
  tag(k.answer, out, 14, out + 14);
}

bool answerDecode(const BulkKeys& k, uint32_t job, const uint8_t* f, size_t n, uint32_t* base, uint32_t* next,
                  uint8_t* have) {
  if (n != ANSWER_LEN || f[0] != ANSWER_MARKER || get32(f + 1) != job) return false;
  if (!tagOk(k.answer, f, 14, f + 14)) return false;
  *base = get32(f + 5);
  *next = get32(f + 9);
  *have = f[13];
  return true;
}

}  // namespace Iface

#endif  // UPD_LORA
