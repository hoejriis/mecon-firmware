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
      rec.n_adm > MAX_MEMBERS || rec.n_roles > MAX_ROLES)
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
  w.u32(rec.authority_revision);
  w.u32(rec.provision_revision);
  w.u32(rec.sync_key_generation);
  w.u8(rec.has_unlock ? 1 : 0);
  w.bytes(rec.unlock_secret, UNLOCK_LEN);
  w.bytes(rec.set_sig, SIG_LEN);
  w.u16(rec.set_len);
  w.bytes(set_text, rec.set_len);
  w.u8(rec.n_roles);
  for (int i = 0; i < rec.n_roles; i++) {
    if (rec.roles[i].role > ROLE_LOCAL) return 0;
    w.u8(rec.roles[i].profile);
    w.u8(rec.roles[i].role);
  }
  w.u8(rec.n_adm);
  for (int i = 0; i < rec.n_adm; i++) {
    w.bytes(rec.adm[i].instance_id, ID_LEN);
    w.u32(rec.adm[i].admitted_revision);
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
  x.authority_revision = p.u32();
  x.provision_revision = p.u32();
  x.sync_key_generation = p.u32();
  uint8_t ua = p.u8();
  if (ua > 1) return false;
  x.has_unlock = ua == 1;
  p.bytes(x.unlock_secret, UNLOCK_LEN);
  p.bytes(x.set_sig, SIG_LEN);
  x.set_len = p.u16();
  if (x.set_len == 0 || x.set_len > MAX_SET_TEXT) return false;
  const uint8_t* st = p.take(x.set_len);
  x.n_roles = p.u8();
  if (x.n_roles > MAX_ROLES) return false;
  for (int i = 0; i < x.n_roles; i++) {
    x.roles[i].profile = p.u8();
    x.roles[i].role = p.u8();
    if (x.roles[i].role > ROLE_LOCAL) return false;
  }
  x.n_adm = p.u8();
  if (x.n_adm > MAX_MEMBERS) return false;
  for (int i = 0; i < x.n_adm; i++) {
    p.bytes(x.adm[i].instance_id, ID_LEN);
    x.adm[i].admitted_revision = p.u32();
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

static const Admission* storedAdmission(const Record& rec, const uint8_t id[ID_LEN]) {
  for (int j = 0; j < rec.n_adm; j++)
    if (memcmp(rec.adm[j].instance_id, id, ID_LEN) == 0) return &rec.adm[j];
  return nullptr;
}

Err checkMembership(const Record& cur, const AuthoritySet& set) {
  for (int i = 0; i < set.n_members; i++) {
    const Member& m = set.members[i];
    const Admission* old = storedAdmission(cur, m.instance_id);
    if (!old) {
      if (m.admitted_revision <= cur.authority_revision) return BAD_AUTHORITY_SET;   // new here, admitted before?
    } else if (m.admitted_revision < old->admitted_revision) {
      return BAD_AUTHORITY_SET;                                                     // admission moved back
    }
  }
  return OK;
}

void setAdmissions(Record* rec, const AuthoritySet& set) {
  memset(rec->adm, 0, sizeof(rec->adm));
  for (int i = 0; i < set.n_members; i++) {
    memcpy(rec->adm[i].instance_id, set.members[i].instance_id, ID_LEN);
    rec->adm[i].admitted_revision = set.members[i].admitted_revision;
  }
  rec->n_adm = set.n_members;
}

// ---- marks

int Marks::indexOf(const uint8_t id[ID_LEN]) const {
  for (int i = 0; i < n; i++)
    if (memcmp(m[i].instance_id, id, ID_LEN) == 0) return i;
  return -1;
}

size_t encodeMarks(const Marks& m, uint32_t generation, uint8_t* out, size_t cap) {
  if (m.n > MAX_MEMBERS) return 0;
  W w = { out, cap, 0, false };
  w.bytes("MDM1", 4);
  w.u32(generation);
  size_t len_at = w.n;
  w.u16(0);
  size_t start = w.n;
  w.u32(m.revision);
  w.u8(m.n);
  for (int i = 0; i < m.n; i++) {
    w.bytes(m.m[i].instance_id, ID_LEN);
    w.u32(m.m[i].admitted_revision);
    w.u32(m.m[i].wm.mark);
  }
  if (w.fail) return 0;
  size_t payload = w.n - start;
  out[len_at] = (uint8_t)payload;
  out[len_at + 1] = (uint8_t)(payload >> 8);
  w.u32(crc32(out, w.n));
  return w.fail ? 0 : w.n;
}

bool decodeMarks(const uint8_t* buf, size_t len, Marks* m, uint32_t* generation) {
  if (!buf || len < FRAME_OVERHEAD || len > MARKS_FRAME || memcmp(buf, "MDM1", 4) != 0) return false;
  R r = { buf, len, 4, false };
  uint32_t gen = r.u32();
  uint16_t payload = r.u16();
  if (FRAME_OVERHEAD + payload != len) return false;
  uint32_t stored = (uint32_t)buf[len - 4] | (uint32_t)buf[len - 3] << 8 | (uint32_t)buf[len - 2] << 16 |
                    (uint32_t)buf[len - 1] << 24;
  if (crc32(buf, len - 4) != stored) return false;
  R p = { buf, len - 4, 10, false };
  memset(m, 0, sizeof(*m));
  m->revision = p.u32();
  m->n = p.u8();
  if (m->n > MAX_MEMBERS) return false;
  for (int i = 0; i < m->n; i++) {
    p.bytes(m->m[i].instance_id, ID_LEN);
    m->m[i].admitted_revision = p.u32();
    m->m[i].wm.mark = p.u32();
  }
  if (p.fail || p.n != len - 4) return false;
  *generation = gen;
  return true;
}

void loadMarks(const Copy& a, const Copy& b, LoadedMarks* out) {
  out->state = LS_ABSENT;
  out->slot = -1;
  out->generation = 0;
  const Copy* c[2] = { &a, &b };
  bool present = false;
  for (int i = 0; i < 2; i++) {
    if (!c[i]->data) continue;
    present = true;
    Marks mk;
    uint32_t g;
    if (!decodeMarks(c[i]->data, c[i]->len, &mk, &g)) continue;
    if (out->state != LS_OK || g > out->generation) {
      out->state = LS_OK;
      out->slot = i;
      out->generation = g;
      out->marks = mk;
    }
  }
  if (out->state != LS_OK && present) out->state = LS_CORRUPT;
}

void nextMarksWrite(const LoadedMarks& cur, int* slot, uint32_t* generation) {
  if (cur.state != LS_OK) {
    *slot = 0;
    *generation = 1;
    return;
  }
  *slot = 1 - cur.slot;
  *generation = cur.generation + 1;
}

int reconcileMarks(const Marks* stored, const AuthoritySet& set, Marks* out) {
  Marks next;
  memset(&next, 0, sizeof(next));
  next.revision = set.authority_revision;
  int blocked = 0;
  for (int i = 0; i < set.n_members; i++) {
    const Member& mem = set.members[i];
    MemberMark& nm = next.m[i];
    memcpy(nm.instance_id, mem.instance_id, ID_LEN);
    nm.admitted_revision = mem.admitted_revision;
    const int j = stored ? stored->indexOf(mem.instance_id) : -1;
    if (!stored) {
      nm.wm.mark = MARK_BLOCKED;                          // the marks were lost: nothing to trust
    } else if (j < 0) {
      // Not in the stored marks: new only if admitted after the set those marks followed, else its mark was lost.
      nm.wm.mark = mem.admitted_revision > stored->revision ? 0 : MARK_BLOCKED;
    } else if (mem.admitted_revision > stored->m[j].admitted_revision) {
      nm.wm.mark = 0;                                     // re-admitted: check 6 refuses what it signed before
    } else {
      nm.wm = stored->m[j].wm;                            // the same admission, or the record fell back: keep it
      nm.admitted_revision = stored->m[j].admitted_revision > mem.admitted_revision ? stored->m[j].admitted_revision
                                                                                     : mem.admitted_revision;
    }
    if (nm.wm.mark == MARK_BLOCKED) blocked++;
  }
  next.n = set.n_members;
  *out = next;
  return blocked;
}

}  // namespace Dep
