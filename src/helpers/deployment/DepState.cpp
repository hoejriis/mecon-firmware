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

DepState* depLoad(const Copy& a, const Copy& b) {
  if (!a.data && !b.data) return nullptr;
  DepState* s = new (std::nothrow) DepState();
  if (!s) return nullptr;
  memset(s, 0, sizeof(*s));
  s->state = LS_CORRUPT;
  s->slot = -1;
  DepState* tmp = new (std::nothrow) DepState();
  if (!tmp) return s;   // cannot check: report corrupt rather than absent
  const Copy* c[2] = { &a, &b };
  for (int i = 0; i < 2; i++) {
    if (!intact(*c[i], tmp)) continue;
    if (s->state != LS_OK || tmp->generation > s->generation) {
      s->state = LS_OK;
      s->slot = i;
      s->generation = tmp->generation;
      s->rec = tmp->rec;
      s->set = tmp->set;
    }
  }
  delete tmp;
  return s;
}

void depFree(DepState* s) { delete s; }

static void hexOut(char* out, const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = H[b[i] >> 4];
    out[2 * i + 1] = H[b[i] & 15];
  }
  out[2 * n] = 0;
}

void depWriteStatus(Iface::JsonWriter& w, const DepState* s, bool hardened) {
  if (!s) return;
  w.key("deployment");
  w.beginObject();
  w.key("contract_version"); w.unum(1);
  if (s->state != LS_OK) {
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
  }
  w.key("auth_rejections"); w.unum(s->auth_rejections);
  w.endObject();
}

}  // namespace Dep
