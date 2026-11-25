#pragma once
// Image bytes over LoRa (docs/contract/LORA_UPDATES.md section 6): the bulk frame a manager sends and the block answer a
// target returns, their keys, tags and exact lengths. Pure logic: the radio, the job and the flash are the caller's, so
// this runs in the native unit tests. tools/mecon_lora_bulk.py is the reference a manager uses, and
// test/fixtures/lora_bulk_v1.json holds vectors both reproduce.
#include <stddef.h>
#include <stdint.h>

namespace Iface {

const uint8_t BULK_MARKER = 0x1C;     // a bulk frame (0x1E is the compact report's: both arrive as anonymous datagrams on a Companion)
const uint8_t ANSWER_MARKER = 0x1F;   // a block answer: a different marker, inside a tag under a different key
const size_t BULK_ROOM = 132;         // the request datagram after its 4-byte tag (LoraLink: LORA_REQ_PAYLOAD + LORA_HDR)
const size_t BULK_HDR = 9;            // marker, job (4), index (4)
const size_t BULK_TAG = 8;
const size_t BULK_DATA = BULK_ROOM - BULK_HDR - BULK_TAG;   // 115 bytes of image in every frame but the last
const size_t ANSWER_LEN = 22;         // marker, job (4), base (4), next (4), have (1), tag (8)
const unsigned BULK_BLOCK_MAX = 8;    // `have` is one byte

// The two keys a transfer token gives, one per direction, so neither kind can stand in for the other.
struct BulkKeys {
  uint8_t bulk[32];
  uint8_t answer[32];
};
void bulkKeys(const uint8_t* token, size_t token_len, BulkKeys* out);
void bulkKeysWipe(BulkKeys* k);

// Frames in a transfer of `transfer_size` bytes (0 for an empty one).
uint32_t bulkFramesTotal(uint32_t transfer_size);
// The exact number of image bytes frame `index` carries, or 0 when the index is past the end.
size_t bulkFrameDataLen(uint32_t transfer_size, uint32_t index);

// A bulk frame into `out` (at least BULK_ROOM): returns its length, or 0 when `n` is not exactly what frame `index` of
// this transfer carries.
size_t bulkFrameEncode(const BulkKeys& k, uint32_t job, uint32_t transfer_size, uint32_t index, const uint8_t* data,
                       size_t n, uint8_t* out, size_t cap);

enum BulkCheck : uint8_t {
  BULK_OK,
  BULK_NOT_BULK,    // another marker (a block answer, a request frame), or too short to hold a header and tag
  BULK_OTHER_JOB,   // a frame of another transfer: dropped before its tag is checked
  BULK_PAST_END,    // an index at or past frames_total
  BULK_BAD_LEN,     // not exactly the length frame `index` carries
  BULK_BAD_TAG,     // a stranger's frame, or one changed on the way
};
const char* bulkCheckName(BulkCheck c);

// Checks a received frame for the open transfer `job` of `transfer_size` bytes. On BULK_OK, `index`, `data` (into `f`)
// and `data_len` say what to write.
BulkCheck bulkFrameDecode(const BulkKeys& k, uint32_t job, uint32_t transfer_size, const uint8_t* f, size_t n,
                          uint32_t* index, const uint8_t** data, size_t* data_len);

// A block answer: `base` is the first frame of the block, bit i of `have` frame base + i, `next` the first frame not
// yet held. `out` takes ANSWER_LEN bytes.
void answerEncode(const BulkKeys& k, uint32_t job, uint32_t base, uint32_t next, uint8_t have, uint8_t out[ANSWER_LEN]);
// False for anything but a well-formed, correctly tagged answer for `job`.
bool answerDecode(const BulkKeys& k, uint32_t job, const uint8_t* f, size_t n, uint32_t* base, uint32_t* next,
                  uint8_t* have);

}  // namespace Iface
