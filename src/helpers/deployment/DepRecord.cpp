#include "DepRecord.h"
#include <string.h>

namespace Dep {

static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  while (n--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

namespace {
struct W {
  uint8_t* b;
  size_t cap, n;
  bool fail;
  void bytes(const void* p, size_t k) {
    if (fail || n + k > cap) { fail = true; return; }
    memcpy(b + n, p, k);
    n += k;
  }
  void u8(uint8_t v) { bytes(&v, 1); }
  void u16(uint16_t v) { uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; bytes(t, 2); }
  void u32(uint32_t v) {
    uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    bytes(t, 4);
  }
};

struct R {
  const uint8_t* b;
  size_t len, n;
  bool fail;
  const uint8_t* take(size_t k) {
    if (fail || n + k > len) { fail = true; return nullptr; }
    const uint8_t* p = b + n;
    n += k;
    return p;
  }
  bool bytes(void* out, size_t k) { const uint8_t* p = take(k); if (p) memcpy(out, p, k); return p != nullptr; }
  uint8_t u8() { const uint8_t* p = take(1); return p ? p[0] : 0; }
  uint16_t u16() { const uint8_t* p = take(2); return p ? (uint16_t)(p[0] | p[1] << 8) : 0; }
  uint32_t u32() {
    const uint8_t* p = take(4);
    return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
  }
};
}  // namespace

size_t encodeRecord(const Record& rec, const char* set_text, uint32_t generation, uint8_t* out, size_t cap) {
  size_t name_len = strnlen(rec.deployment_name, MAX_NAME + 1);
  if (name_len < 1 || name_len > MAX_NAME || rec.set_len == 0 || rec.set_len > MAX_SET_TEXT || !set_text ||
      rec.n_replay > MAX_MEMBERS)
    return 0;
  W w = { out, cap, 0, false };
  w.bytes("MDR1", 4);
  w.u32(generation);
  size_t len_at = w.n;
  w.u16(0);   // payload length, patched below
  size_t start = w.n;
  w.u8(rec.contract_version);
  w.bytes(rec.deployment_id, ID_LEN);
  w.u8((uint8_t)name_len);
  w.bytes(rec.deployment_name, name_len);
  w.bytes(rec.dak, KEY_LEN);
  w.u32(rec.authority_epoch);
  w.u32(rec.provision_revision);
  w.u32(rec.sync_key_generation);
  w.u8(rec.has_usb_auth ? 1 : 0);
  w.bytes(rec.usb_auth_hash, 32);
  w.bytes(rec.set_sig, SIG_LEN);
  w.u16(rec.set_len);
  w.bytes(set_text, rec.set_len);
  w.u8(rec.n_replay);
  for (int i = 0; i < rec.n_replay; i++) {
    w.bytes(rec.replay[i].instance_id, ID_LEN);
    for (int c = 0; c < JOB_CLASSES; c++) {
      w.u32(rec.replay[i].wm[c].mark);
      w.u32(rec.replay[i].wm[c].mask);
    }
  }
  if (w.fail) return 0;
  size_t payload = w.n - start;
  out[len_at] = (uint8_t)payload;
  out[len_at + 1] = (uint8_t)(payload >> 8);
  w.u32(crc32(out, w.n));
  return w.fail ? 0 : w.n;
}

bool decodeRecord(const uint8_t* buf, size_t len, Record* rec, const char** set_text, uint32_t* generation) {
  if (!buf || len < FRAME_OVERHEAD || len > MAX_FRAME || memcmp(buf, "MDR1", 4) != 0) return false;
  R r = { buf, len, 4, false };
  uint32_t gen = r.u32();
  uint16_t payload = r.u16();
  if (FRAME_OVERHEAD + payload != len) return false;   // truncated, or trailing bytes
  uint32_t stored = (uint32_t)buf[len - 4] | (uint32_t)buf[len - 3] << 8 | (uint32_t)buf[len - 2] << 16 |
                    (uint32_t)buf[len - 1] << 24;
  if (crc32(buf, len - 4) != stored) return false;

  R p = { buf, len - 4, 10, false };
  Record& x = *rec;
  memset(&x, 0, sizeof(x));
  x.contract_version = p.u8();
  p.bytes(x.deployment_id, ID_LEN);
  uint8_t nl = p.u8();
  if (nl < 1 || nl > MAX_NAME) return false;
  p.bytes(x.deployment_name, nl);
  x.deployment_name[nl] = 0;
  p.bytes(x.dak, KEY_LEN);
  x.authority_epoch = p.u32();
  x.provision_revision = p.u32();
  x.sync_key_generation = p.u32();
  uint8_t ua = p.u8();
  if (ua > 1) return false;
  x.has_usb_auth = ua == 1;
  p.bytes(x.usb_auth_hash, 32);
  p.bytes(x.set_sig, SIG_LEN);
  x.set_len = p.u16();
  if (x.set_len == 0 || x.set_len > MAX_SET_TEXT) return false;
  const uint8_t* st = p.take(x.set_len);
  x.n_replay = p.u8();
  if (x.n_replay > MAX_MEMBERS) return false;
  for (int i = 0; i < x.n_replay; i++) {
    p.bytes(x.replay[i].instance_id, ID_LEN);
    for (int c = 0; c < JOB_CLASSES; c++) {
      x.replay[i].wm[c].mark = p.u32();
      x.replay[i].wm[c].mask = p.u32();
    }
  }
  if (p.fail || p.n != len - 4) return false;   // every payload byte accounted for
  *set_text = reinterpret_cast<const char*>(st);
  *generation = gen;
  return true;
}

void loadRecord(const Copy& a, const Copy& b, Loaded* out) {
  out->state = LS_ABSENT;
  out->slot = -1;
  out->generation = 0;
  out->set_text = nullptr;
  const Copy* c[2] = { &a, &b };
  bool present = false;
  for (int i = 0; i < 2; i++) {
    if (!c[i]->data) continue;
    present = true;
    Record r;
    const char* t;
    uint32_t g;
    if (!decodeRecord(c[i]->data, c[i]->len, &r, &t, &g)) continue;
    if (out->state != LS_OK || g > out->generation) {
      out->state = LS_OK;
      out->slot = i;
      out->generation = g;
      out->rec = r;
      out->set_text = t;
    }
  }
  if (out->state != LS_OK && present) out->state = LS_CORRUPT;
}

void nextWrite(const Loaded& cur, int* slot, uint32_t* generation) {
  if (cur.state != LS_OK) {
    *slot = 0;
    *generation = 1;
    return;
  }
  *slot = 1 - cur.slot;
  *generation = cur.generation + 1;
}

void reconcileReplay(Record* rec, const AuthoritySet& set) {
  IssuerReplay next[MAX_MEMBERS];
  memset(next, 0, sizeof(next));
  for (int i = 0; i < set.n_members; i++) {
    memcpy(next[i].instance_id, set.members[i].instance_id, ID_LEN);
    for (int j = 0; j < rec->n_replay; j++)
      if (memcmp(rec->replay[j].instance_id, set.members[i].instance_id, ID_LEN) == 0)
        memcpy(next[i].wm, rec->replay[j].wm, sizeof(next[i].wm));
  }
  memcpy(rec->replay, next, sizeof(next));
  rec->n_replay = set.n_members;
}

}  // namespace Dep
