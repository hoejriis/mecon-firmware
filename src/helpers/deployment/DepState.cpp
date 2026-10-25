#include "DepState.h"
#include <new>
#include <string.h>

namespace Dep {

// Decodes one copy and checks its stored set still verifies against its stored key.
static bool intact(const Copy& c, DepState* into) {
  if (!c.data) return false;
  memset(&into->set, 0, sizeof(into->set));   // a previous copy's parsed set must never stand in for this one's
  const char* text;
  if (!decodeRecord(c.data, c.len, &into->rec, &text, &into->generation)) return false;
  SetContext sc = { into->rec.dak, into->rec.deployment_id, into->rec.authority_epoch, false, 0 };
  return acceptSet(text, into->rec.set_len, into->rec.set_sig, sc, &into->set) == OK &&
         into->set.authority_revision == into->rec.authority_revision;
}

LoadState depLoad(const Copy& a, const Copy& b, DepState** out) {
  *out = nullptr;
  if (!a.data && !b.data) return LS_ABSENT;
  DepState* s = new (std::nothrow) DepState();
  DepState* tmp = s ? new (std::nothrow) DepState() : nullptr;
  if (!s || !tmp) {   // cannot check: corrupt, never absent
    delete s;
    return LS_CORRUPT;
  }
  memset(s, 0, sizeof(*s));
  s->state = LS_CORRUPT;
  s->slot = -1;
  const Copy* c[2] = { &a, &b };
  bool ok[2] = { false, false };
  for (int i = 0; i < 2; i++) {
    if (!intact(*c[i], tmp)) continue;
    ok[i] = true;
    if (s->state != LS_OK || tmp->generation > s->generation) {
      s->state = LS_OK;
      s->slot = i;
      s->generation = tmp->generation;
      s->rec = tmp->rec;
      s->set = tmp->set;
    }
  }
  if (s->state != LS_OK) {   // corrupt: the unlock secret of the newest copy that still passes its frame check
    uint32_t best = 0;
    for (int i = 0; i < 2; i++) {
      const char* text;
      uint32_t gen;
      if (!c[i]->data || !decodeRecord(c[i]->data, c[i]->len, &tmp->rec, &text, &gen) || !tmp->rec.has_unlock) continue;
      if (s->has_salvage && gen <= best) continue;
      s->has_salvage = true;
      best = gen;
      memcpy(s->salvage, tmp->rec.unlock_secret, UNLOCK_LEN);
    }
  }
  memset(tmp, 0, sizeof(*tmp));
  delete tmp;
  if (s->state == LS_OK) {
    const int other = 1 - s->slot;
    s->other_damaged = c[other]->data && !ok[other];
  }
  *out = s;
  return s->state;
}

void depFree(DepState* s) {
  if (s) memset(s, 0, sizeof(*s));   // the record holds the unlock secret
  delete s;
}

void depLoadMarks(DepState* s, const Copy& a, const Copy& b) {
  if (!s || s->state != LS_OK) return;
  LoadedMarks lm;
  memset(&lm, 0, sizeof(lm));   // loadMarks leaves marks unset when they are absent or corrupt
  loadMarks(a, b, &lm);
  s->marks_lost = lm.state != LS_OK;
  s->marks_slot = lm.state == LS_OK ? lm.slot : -1;
  s->marks_generation = lm.state == LS_OK ? lm.generation : 0;
  s->blocked = reconcileMarks(s->marks_lost ? nullptr : &lm.marks, s->set, &s->marks);
  s->marks_dirty = s->marks_lost || memcmp(&s->marks, &lm.marks, sizeof(Marks)) != 0;
}

static void hexOut(char* out, const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = H[b[i] >> 4];
    out[2 * i + 1] = H[b[i] & 15];
  }
  out[2 * n] = 0;
}

void depWriteStatus(Iface::JsonWriter& w, LoadState state, const DepState* s, bool hardened) {
  if (state == LS_ABSENT) return;
  w.key("deployment");
  w.beginObject();
  w.key("contract_version"); w.unum(1);
  if (state != LS_OK || !s) {
    // Present, so it is never read as legacy; nothing that cannot be read is reported.
    w.key("state"); w.str("corrupt");
  } else {
    char id[2 * ID_LEN + 1];
    hexOut(id, s->rec.deployment_id, ID_LEN);
    w.key("deployment_id"); w.str(id);
    w.key("authority_epoch"); w.unum(s->rec.authority_epoch);
    w.key("authority_revision"); w.unum(s->rec.authority_revision);
    w.key("provision_revision"); w.unum(s->rec.provision_revision);
    w.key("sync_key_generation"); w.unum(s->rec.sync_key_generation);
    w.key("posture"); w.str(hardened ? "hardened" : "normal");
    w.key("fallback"); w.boolean(s->other_damaged);
    w.key("blocked_members"); w.unum((uint32_t)s->blocked);
  }
  w.key("auth_rejections"); w.unum(s ? s->auth_rejections : 0);
  w.endObject();
}

}  // namespace Dep
