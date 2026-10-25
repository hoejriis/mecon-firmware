// The Deployment authority layer of the Companion's device interface (docs/contract/AUTHORITY.md): the record and its
// sequence marks loaded at boot, signed requests checked and replay-protected before they run, the channel nonces, and the
// three deployment operations. The checks themselves are the pure helpers in src/helpers/deployment/; this file is the
// glue to storage, the interface core and the CLI privilege table. Fail closed throughout: anything that cannot be decided
// is a refusal, and nothing here prints or returns a secret.
#include "MeconIface.h"
#ifdef ENABLE_MECON_IFACE
#include "MyMesh.h"
#include <helpers/CryptoRNG.h>
#include <helpers/deployment/DepProvision.h>
#include <helpers/deployment/DepReplay.h>
#include <new>
#include <stdlib.h>
#include <string.h>

using namespace Iface;

namespace {

MeconIface* self(OpContext& c) { return static_cast<MeconIface*>(c.user); }

void hexTo(char* out, const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
  out[2 * n] = 0;
}

void base64Of(char* out, const uint8_t* in, size_t n) {   // standard alphabet with padding
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < n) v |= in[i + 2];
    out[o++] = T[(v >> 18) & 63];
    out[o++] = T[(v >> 12) & 63];
    out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < n ? T[v & 63] : '=';
  }
  out[o] = 0;
}

const size_t ROT_TEXT_MAX = 512;   // a rotation statement is about 250 bytes

// ---- field readers (all strict: a field that is present but wrong is a malformed request)

bool getHex(const JsonDoc& d, int obj, const char* key, uint8_t* out, size_t n) {
  char buf[2 * Dep::KEY_LEN + 1];
  int t = jsonFind(d, obj, key);
  return t >= 0 && 2 * n + 1 <= sizeof(buf) && jsonGetString(d, t, buf, 2 * n + 1, CS_ID, 2 * n) &&
         Dep::hexToBytes(buf, 2 * n, out, n);
}

bool getU32(const JsonDoc& d, int obj, const char* key, uint32_t* out) {
  int64_t v;
  int t = jsonFind(d, obj, key);
  if (t < 0 || !jsonGetInt(d, t, 0, 0xFFFFFFFFLL, &v)) return false;
  *out = (uint32_t)v;
  return true;
}

bool getSig(const JsonDoc& d, int obj, const char* key, uint8_t out[Dep::SIG_LEN]) {
  char b64[89];
  int t = jsonFind(d, obj, key);
  return t >= 0 && jsonGetString(d, t, b64, sizeof(b64), CS_ANY, 88) && Dep::base64ToSig(b64, 88, out);
}

// The id of a verified body, for an answer that must carry it.
void bodyId(const char* body, size_t len, char id[ID_MAX + 1]) {
  id[0] = 0;
  RequestDoc* d = new (std::nothrow) RequestDoc();
  if (!d) return;
  Request r;
  parseRequest(*d, body, len, r);
  if (r.has_id) { strncpy(id, r.id, ID_MAX); id[ID_MAX] = 0; }
  delete d;
}

}  // namespace

// Everything one deployment operation needs, on the heap for its duration (about 7 KB: the loop task's stack has 5 KB).
struct MeconIface::DepWork {
  uint8_t frame[Dep::MAX_FRAME];         // the stored copy as read, then the frame being written
  Dep::Loaded cur;                       // the record as stored; set_text points into frame while it is unwritten
  Dep::Record out;                       // the record to commit
  Dep::AuthoritySet set;                 // its parsed set
  Dep::ProvisionPayload p;
  char name[2 * Dep::MAX_NAME + 2];
  char set_text[Dep::MAX_SET_TEXT + 1];
  size_t set_len;
  uint8_t set_sig[Dep::SIG_LEN];
  char* rot_pool;                        // n_rot texts of ROT_TEXT_MAX bytes, allocated only when rotations come
  uint8_t n_rot;
  Dep::Rotation rot[Dep::MAX_ROTATIONS];
  DepWork() { memset(this, 0, sizeof(*this)); }
  ~DepWork() { free(rot_pool); memset(frame, 0, sizeof(frame)); }   // the record holds the unlock secret
};

// ---- storage

// The current record's winning copy, read back from storage into w->cur (state LS_ABSENT with no record). False when the
// stored copy no longer reads as the one loaded: the caller refuses rather than act on a record it cannot see.
bool MeconIface::readCurrent(DepWork* w) {
  w->cur.state = Dep::LS_ABSENT;
  if (_dep_ls != Dep::LS_OK || !_dep) return true;
  size_t len = 0;
  bool exists = false;
  if (!_mesh->_store->readDeploymentCopy(_dep->slot, w->frame, sizeof(w->frame), &len, &exists) || !exists) return false;
  const char* text;
  uint32_t gen;
  if (!Dep::decodeRecord(w->frame, len, &w->cur.rec, &text, &gen) || gen != _dep->generation) return false;
  w->cur.state = Dep::LS_OK;
  w->cur.slot = _dep->slot;
  w->cur.generation = gen;
  w->cur.set_text = text;
  return true;
}

// Writes the marks in _dep->marks to the next marks slot.
bool MeconIface::persistMarks() {
  if (!_dep) return false;
  Dep::LoadedMarks lm;
  memset(&lm, 0, sizeof(lm));
  lm.state = _dep->marks_slot >= 0 ? Dep::LS_OK : Dep::LS_CORRUPT;
  lm.slot = _dep->marks_slot;
  lm.generation = _dep->marks_generation;
  int slot;
  uint32_t gen;
  Dep::nextMarksWrite(lm, &slot, &gen);
  uint8_t buf[Dep::MARKS_FRAME];
  const size_t n = Dep::encodeMarks(_dep->marks, gen, buf, sizeof(buf));
  if (!n || !_mesh->_store->writeDeploymentCopy(slot, buf, n, DataStore::DEP_MARKS)) return false;
  _dep->marks_slot = slot;
  _dep->marks_generation = gen;
  _dep->marks_dirty = false;
  return true;
}

// The marks for a newly committed set, written after the record (AUTHORITY.md section 4): a member still present at its
// admission keeps its mark, a re-admitted one restarts at 0, and with the marks lost nobody is trusted.
bool MeconIface::saveMarksFor(const Dep::AuthoritySet& set) {
  if (!_dep) return false;
  Dep::Marks next;
  const int blocked = Dep::reconcileMarks(_dep->marks_lost ? nullptr : &_dep->marks, set, &next);
  _dep->marks = next;
  _dep->blocked = blocked;
  _dep->marks_dirty = true;
  if (!persistMarks()) return false;
  _dep->marks_lost = false;
  return true;
}

// Record first: the new record goes to the copy that does not hold the current one, and is read back. Only then does the
// in-memory state move.
bool MeconIface::commit(DepWork* w) {
  const bool first = _dep == nullptr;
  Dep::DepState* d = _dep;
  if (first) {
    d = new (std::nothrow) Dep::DepState();
    if (!d) return false;
    memset(d, 0, sizeof(*d));
    d->marks_slot = -1;
  }
  int slot;
  uint32_t gen;
  Dep::nextWrite(w->cur, &slot, &gen);
  bool ok = false;
  const size_t n = Dep::encodeRecord(w->out, w->set_text, gen, w->frame, sizeof(w->frame));
  if (n && _mesh->_store->writeDeploymentCopy(slot, w->frame, n)) {
    size_t len = 0;
    bool exists = false;
    Dep::Record back;
    const char* text;
    uint32_t g;
    ok = _mesh->_store->readDeploymentCopy(slot, w->frame, sizeof(w->frame), &len, &exists) && exists &&
         Dep::decodeRecord(w->frame, len, &back, &text, &g) && g == gen && back.set_len == w->out.set_len &&
         memcmp(text, w->set_text, back.set_len) == 0;
  }
  if (!ok) {
    if (first) delete d;
    return false;
  }
  d->state = Dep::LS_OK;
  d->slot = slot;
  d->generation = gen;
  d->other_damaged = false;
  d->rec = w->out;
  d->set = w->set;
  _dep = d;
  _dep_ls = Dep::LS_OK;
  return true;
}

// ---- boot

void MeconIface::deployLoad() {
  _dep = nullptr;
  _dep_ls = Dep::LS_ABSENT;
  DataStore* st = _mesh->_store;
  // Whether anything exists at all, without reading it: an existing copy that cannot be read is corrupt, never absent.
  bool any = false;
  for (int slot = 0; slot < 2; slot++) {
    size_t len;
    bool exists = false;
    uint8_t probe;
    st->readDeploymentCopy(slot, &probe, 0, &len, &exists);
    any = any || exists;
  }
  if (!any) return;
  uint8_t* buf[2] = { static_cast<uint8_t*>(malloc(Dep::MAX_FRAME)), static_cast<uint8_t*>(malloc(Dep::MAX_FRAME)) };
  uint8_t* mbuf[2] = { static_cast<uint8_t*>(malloc(Dep::MARKS_FRAME)), static_cast<uint8_t*>(malloc(Dep::MARKS_FRAME)) };
  if (!buf[0] || !buf[1] || !mbuf[0] || !mbuf[1]) {
    _dep_ls = Dep::LS_CORRUPT;   // cannot check: never absent
  } else {
    Dep::Copy c[2];
    for (int slot = 0; slot < 2; slot++) {
      size_t len = 0;
      bool exists = false;
      const bool ok = st->readDeploymentCopy(slot, buf[slot], Dep::MAX_FRAME, &len, &exists);
      c[slot].data = exists ? buf[slot] : nullptr;
      c[slot].len = ok ? len : 0;
    }
    _dep_ls = Dep::depLoad(c[0], c[1], &_dep);
    if (_dep_ls == Dep::LS_OK) {
      Dep::Copy m[2];
      for (int slot = 0; slot < 2; slot++) {
        size_t len = 0;
        bool exists = false;
        const bool ok = st->readDeploymentCopy(slot, mbuf[slot], Dep::MARKS_FRAME, &len, &exists, DataStore::DEP_MARKS);
        m[slot].data = exists ? mbuf[slot] : nullptr;
        m[slot].len = ok ? len : 0;
      }
      Dep::depLoadMarks(_dep, m[0], m[1]);
      // Reconciled marks that differ from the stored ones are written back before any signed request is served; if the
      // write fails, the first signed request retries it and is answered busy until it succeeds.
      if (_dep->marks_dirty) persistMarks();
    }
  }
  for (uint8_t* p : buf) if (p) { memset(p, 0, Dep::MAX_FRAME); free(p); }   // the frames hold the unlock secret
  for (uint8_t* p : mbuf) free(p);
}

void MeconIface::writeDeploymentStatus(JsonWriter& w, bool hardened) { Dep::depWriteStatus(w, _dep_ls, _dep, hardened); }

const char* MeconIface::helloNonce() {
  if (_dep_ls == Dep::LS_ABSENT) return nullptr;
  const char* n = nonceOf(_cur_link);
  return n[0] ? n : nullptr;
}

// ---- channel nonces

void MeconIface::issueNonce(char slot[33]) {
  uint8_t b[16];
  memset(b, 0, sizeof(b));
  CryptoRNG rng;   // no fallback: a nonce from a weak source would be worse than none
  rng.random(b, sizeof(b));
  bool nonzero = false;
  for (uint8_t x : b) nonzero = nonzero || x != 0;
  if (!nonzero) { slot[0] = 0; return; }   // the RNG gave nothing: no nonce, so a sensitive request is refused
  hexTo(slot, b, sizeof(b));
  memset(b, 0, sizeof(b));
}

// ---- signed requests

bool MeconIface::isWrapper(const char* m, size_t n) {
  static const char K[] = "\"mecon_auth\"";
  const size_t kl = sizeof(K) - 1;
  bool found = false;
  for (size_t i = 0; i + kl <= n && !found; i++) found = m[i] == '"' && memcmp(m + i, K, kl) == 0;
  if (!found) return false;
  RequestDoc* d = new (std::nothrow) RequestDoc();
  if (!d) return true;   // cannot tell: the signed path refuses it
  const bool w = jsonParse(*d, m, n) == J_OK && jsonFind(*d, 0, "mecon_auth") >= 0;
  delete d;
  return w;
}

int MeconIface::dispatchSigned(const Cli::Context& uctx, uint8_t caller, Link* link, const char* msg, size_t len,
                               char* out, size_t cap) {
  if (_dep_ls == Dep::LS_ABSENT) return writeResult(out, cap, "", ST_REFUSED, nullptr, "unauthenticated");
  if (_dep_ls != Dep::LS_OK || !_dep) {
    if (_dep) _dep->auth_rejections++;
    return writeResult(out, cap, "", ST_REFUSED, nullptr, "deployment_record_corrupt");
  }
  char* body = static_cast<char*>(malloc(Dep::MAX_WRAPPED + 1));
  if (!body) return writeResult(out, cap, "", ST_BUSY, nullptr, "no-memory");
  Dep::Verified v;
  memset(&v, 0, sizeof(v));
  const char* nonce = nonceOf(link);
  const Dep::WrapErr e = Dep::verifyWrapped(msg, len, _dep->rec, _dep->set, _mesh->self_id.pub_key, Dep::contractOpClass,
                                            nonce[0] ? nonce : nullptr, body, Dep::MAX_WRAPPED + 1, &v);
  int n;
  if (e != Dep::WR_OK) {
    _dep->auth_rejections++;
    n = writeResult(out, cap, "", ST_REFUSED, nullptr, Dep::wrapErrCode(e));
    free(body);
    return n;
  }
  char id[ID_MAX + 1];
  bodyId(v.body, v.body_len, id);
  char key[24];   // the result cache key of this request: '#' is outside the id charset, so no unsigned id can collide
  key[0] = 0;

  if (v.cls != Dep::OPC_PLAIN) {
    // Marks that could not be written back at boot, or after a set change, are retried before anything is accepted.
    if (_dep->marks_dirty && !persistMarks()) {
      n = writeResult(out, cap, id, ST_BUSY, nullptr, "storage");
      free(body);
      return n;
    }
    if (v.member < 0 || v.member >= _dep->marks.n ||
        memcmp(_dep->marks.m[v.member].instance_id, _dep->set.members[v.member].instance_id, Dep::ID_LEN) != 0) {
      n = writeResult(out, cap, id, ST_BUSY, nullptr, "marks");   // marks and set disagree: run nothing
      free(body);
      return n;
    }
    Dep::Watermark& wm = _dep->marks.m[v.member].wm;
    const Dep::ReplayVerdict rv = Dep::replayCheck(wm, v.job_id);
    key[0] = '#';
    hexTo(key + 1, _dep->set.members[v.member].instance_id, 6);
    {
      uint8_t j[4] = { (uint8_t)(v.job_id >> 24), (uint8_t)(v.job_id >> 16), (uint8_t)(v.job_id >> 8), (uint8_t)v.job_id };
      hexTo(key + 13, j, 4);
    }
    if (rv == Dep::RV_TOO_OLD) {
      _dep->auth_rejections++;
      n = writeResult(out, cap, id, ST_REFUSED, nullptr, "replay");
      free(body);
      return n;
    }
    if (rv == Dep::RV_DUPLICATE) {
      const char* data;
      size_t dl;
      if (_cache && _cache->find(key, &data, &dl) == ResultCache::HIT && dl < cap) {
        memcpy(out, data, dl);
        out[dl] = 0;
        n = (int)dl;
      } else {
        n = writeResult(out, cap, id, ST_FAILED, nullptr, "duplicate");   // it ran (or a cut lost it); read the state
      }
      free(body);
      return n;
    }
    // New: the mark is persisted BEFORE the request runs, so a redelivery after a power cut cannot run it twice. A write
    // that fails runs nothing and leaves the mark where it was.
    const uint32_t old = wm.mark;
    Dep::markAccepted(&wm, v.job_id);
    if (!persistMarks()) {
      wm.mark = old;
      n = writeResult(out, cap, id, ST_BUSY, nullptr, "storage");
      free(body);
      return n;
    }
  }

  // Run it as a normal v1.0 request for the signed principal on this transport (Context.remote follows the transport).
  const Cli::Context sctx(uctx.transport, Cli::A_SIGNED, uctx.posture, uctx.mgmt);
  _signed = true;
  n = runDispatch(sctx, caller, link, v.body, v.body_len, out, cap, nullptr);   // job ids guard replay; no id cache
  _signed = false;
  if (n > 0 && key[0] && _cache) _cache->store(key, out, (size_t)n);
  if (v.cls == Dep::OPC_SENSITIVE) issueNonce(nonceOf(link));   // used once
  free(body);
  return n;
}

// ---- operations

namespace {

void hexOfId(char* out, const uint8_t id[Dep::ID_LEN]) { hexTo(out, id, Dep::ID_LEN); }

}  // namespace

// The fields of a stored Deployment (AUTHORITY.md section 6.3), shared by deployment.get and the read-back of a
// provisioning. Nothing secret: the unlock secret and the DAK-signed set's private side never exist here.
static void writeFields(JsonWriter& w, const Dep::DepState& s, const Dep::Record& rec, const char* set_text, bool full) {
  char id[2 * Dep::ID_LEN + 1];
  hexOfId(id, rec.deployment_id);
  w.key("state"); w.str("provisioned");
  w.key("deployment_id"); w.str(id);
  w.key("deployment_name"); w.str(rec.deployment_name);
  w.key("authority_epoch"); w.unum(rec.authority_epoch);
  w.key("authority_revision"); w.unum(rec.authority_revision);
  w.key("provision_revision"); w.unum(rec.provision_revision);
  w.key("fallback"); w.boolean(s.other_damaged);
  if (full) {
    // A set can be longer than one result carries (the pages come later): then only its length is reported.
    size_t esc = rec.set_len;
    for (size_t i = 0; i < rec.set_len; i++) esc += (set_text[i] == '"' || set_text[i] == '\\') ? 1 : 0;
    if (esc <= 900) {
      char sig[89];
      base64Of(sig, rec.set_sig, Dep::SIG_LEN);
      w.key("authority_set"); w.str(set_text, rec.set_len);
      w.key("authority_set_sig"); w.str(sig);
    } else {
      w.key("authority_set_len"); w.unum(rec.set_len);
    }
  }
  w.key("members"); w.beginArray();
  for (int i = 0; i < s.set.n_members && i < s.marks.n; i++) {
    hexOfId(id, s.set.members[i].instance_id);
    w.beginObject();
    w.key("instance_id"); w.str(id);
    w.key("admitted_revision"); w.unum(s.set.members[i].admitted_revision);
    w.key("mark"); w.unum(s.marks.m[i].wm.mark);   // MARK_BLOCKED (4294967295) until the member is re-admitted
    w.endObject();
  }
  w.endArray();
}

Status MeconIface::opDeployGet(OpContext& c) {
  MeconIface* m = self(c);
  JsonWriter& w = *c.out;
  if (m->_dep_ls == Dep::LS_ABSENT) { w.key("state"); w.str("none"); return ST_OK; }
  if (m->_dep_ls != Dep::LS_OK || !m->_dep) { w.key("state"); w.str("corrupt"); return ST_OK; }
  DepWork* k = new (std::nothrow) DepWork();
  if (!k) { c.error = "no-memory"; return ST_BUSY; }
  Status st = ST_OK;
  if (m->readCurrent(k)) writeFields(w, *m->_dep, k->cur.rec, k->cur.set_text, true);
  else { c.error = "storage"; st = ST_FAILED; }
  delete k;
  return st;
}

namespace {

// The `deployment` object of a provisioning into p. err is the contract code to refuse with; malformed is a request that
// is not one (invalid, nothing ran).
struct ProvParsed { const char* err; bool malformed; };

ProvParsed parseProvision(const JsonDoc& d, int args, Dep::ProvisionPayload* p,
                          char* name, size_t name_cap, char* set_text, size_t set_cap, size_t* set_len,
                          uint8_t set_sig[Dep::SIG_LEN]) {
  const int o = jsonFind(d, args, "deployment");
  if (!isObject(d, o)) return {nullptr, true};
  if (!getU32(d, o, "contract_version", &p->contract_version)) return {nullptr, true};
  if (!getHex(d, o, "deployment_id", p->deployment_id, Dep::ID_LEN)) return {nullptr, true};
  if (!getHex(d, o, "node_public_key", p->node_public_key, Dep::KEY_LEN)) return {nullptr, true};
  if (!getU32(d, o, "provision_revision", &p->provision_revision)) return {nullptr, true};
  if (!getHex(d, o, "dak", p->dak, Dep::KEY_LEN)) return {nullptr, true};
  if (!getU32(d, o, "authority_epoch", &p->authority_epoch)) return {nullptr, true};
  if (!getSig(d, o, "authority_set_sig", set_sig)) return {nullptr, true};
  memcpy(p->set_sig, set_sig, Dep::SIG_LEN);
  int t = jsonFind(d, o, "deployment_name");
  if (t < 0) return {nullptr, true};
  if (!jsonGetString(d, t, name, name_cap, CS_ANY, 0)) return {"bad_deployment_name", false};
  p->name = name;
  p->name_len = strlen(name);
  t = jsonFind(d, o, "authority_set");
  if (t < 0) return {nullptr, true};
  if (!jsonGetString(d, t, set_text, set_cap, CS_ANY, 1)) return {"bad_authority_set", false};
  *set_len = strlen(set_text);
  p->set_text = set_text;
  p->set_len = *set_len;
  p->n_rotations = 0;   // a provisioning carries none (contract section 6.1)
  t = jsonFind(d, o, "unlock_secret");
  if (t >= 0) {
    if (!getHex(d, o, "unlock_secret", p->unlock_secret, Dep::UNLOCK_LEN)) return {nullptr, true};
    p->has_unlock = true;
  }
  t = jsonFind(d, o, "broker_roles");
  if (t >= 0) {
    if (!isArray(d, t) || d.tok[t].size > Dep::MAX_ROLES) return {"bad_broker_roles", false};
    for (int i = 0; i < d.tok[t].size; i++) {
      const int e = jsonElem(d, t, i);
      char role[16];
      int64_t prof;
      const int tp = jsonFind(d, e, "profile"), tr = jsonFind(d, e, "role");
      if (!isObject(d, e) || tp < 0 || tr < 0 || !jsonGetInt(d, tp, 0, 255, &prof) ||
          !jsonGetString(d, tr, role, sizeof(role), CS_NAME, 1))
        return {"bad_broker_roles", false};
      p->roles[i].profile = (uint8_t)prof;
      if (strcmp(role, "primary") == 0) p->roles[i].role = Dep::ROLE_PRIMARY;
      else if (strcmp(role, "secondary") == 0) p->roles[i].role = Dep::ROLE_SECONDARY;
      else if (strcmp(role, "local") == 0) p->roles[i].role = Dep::ROLE_LOCAL;
      else return {"bad_broker_roles", false};
      p->n_roles = (uint8_t)(i + 1);
    }
  }
  return {nullptr, false};
}

bool profileExists(void* user, uint8_t index) {
#ifdef ENABLE_MQTT
  return static_cast<MyMesh*>(user)->getNodePrefs()->mqtt_profiles.configured(index);
#else
  (void)user; (void)index;
  return false;
#endif
}

}  // namespace

Status MeconIface::opDeployProvision(OpContext& c) {
  MeconIface* m = self(c);
  JsonWriter& w = *c.out;
  if (m->_dep_ls == Dep::LS_CORRUPT) {   // never reopen a first provisioning over something that cannot be read
    if (m->_dep) m->_dep->auth_rejections++;
    c.error = "deployment_record_corrupt";
    return ST_REFUSED;
  }
  const bool has = m->_dep_ls == Dep::LS_OK;
  const Cli::Context& cx = m->_ctx;
  // Unsigned first provisioning: trust on first use, accepted only from a caller that is physically present.
  const bool present = (cx.transport == Cli::T_USB && cx.auth == Cli::A_CABLE) ||
                       (cx.transport == Cli::T_BLE && cx.auth == Cli::A_PAIRED_PIN);
  if (!has && !m->_signed && !present) { c.error = "unauthenticated"; return ST_REFUSED; }

  DepWork* k = new (std::nothrow) DepWork();
  if (!k) { c.error = "no-memory"; return ST_BUSY; }
  Status st = ST_REFUSED;
  const ProvParsed pp = parseProvision(*c.doc, c.req->args, &k->p, k->name, sizeof(k->name), k->set_text,
                                       sizeof(k->set_text), &k->set_len, k->set_sig);
  if (pp.malformed) { c.error = "bad-args"; st = ST_INVALID; goto done; }
  if (pp.err) { c.error = pp.err; goto done; }
  if (!m->readCurrent(k)) { c.error = "storage"; st = ST_FAILED; goto done; }
  {
    Dep::ProvisionContext pc = { &k->cur, m->_mesh->self_id.pub_key, profileExists, m->_mesh };
    const Dep::ProvErr pe = Dep::checkProvision(k->p, pc, &k->out, &k->set);
    if (has && !m->_signed) {
      // Held a record, so only a signed request changes it. An unsigned byte-identical resend gets the read-back; anything
      // else says nothing about why (no oracle for an unauthenticated caller).
      if (pe != Dep::P_DUPLICATE) {
        if (m->_dep) m->_dep->auth_rejections++;
        c.error = "unauthenticated";
        goto done;
      }
    }
    if (pe == Dep::P_DUPLICATE) {
      w.key("duplicate"); w.boolean(true);
      writeFields(w, *m->_dep, k->cur.rec, k->cur.set_text, false);
      st = ST_OK;
      goto done;
    }
    if (pe != Dep::P_OK) {
      if (m->_dep) m->_dep->auth_rejections++;
      c.error = Dep::provErrCode(pe);
      goto done;
    }
    // Commit: the record, then its marks, then the answer read back from storage.
    if (!m->commit(k)) { c.error = "storage"; st = ST_BUSY; goto done; }
    if (!m->saveMarksFor(k->set)) { c.error = "marks"; st = ST_BUSY; goto done; }
    if (!m->readCurrent(k)) { c.error = "storage"; st = ST_FAILED; goto done; }
    writeFields(w, *m->_dep, k->cur.rec, k->cur.set_text, false);
    st = ST_OK;
  }
done:
  delete k;
  return st;
}

Status MeconIface::opDeployUpdate(OpContext& c) {
  MeconIface* m = self(c);
  JsonWriter& w = *c.out;
  if (m->_dep_ls == Dep::LS_CORRUPT) {
    if (m->_dep) m->_dep->auth_rejections++;
    c.error = "deployment_record_corrupt";
    return ST_REFUSED;
  }
  if (m->_dep_ls != Dep::LS_OK) { c.error = "untrusted_authority_key"; return ST_REFUSED; }   // nothing pinned to verify against
  if (m->_have_update && (uint32_t)(millis() - m->_last_update_ms) < 1000) {   // flash wear
    c.error = "rate-limited";
    return ST_BUSY;
  }
  DepWork* k = new (std::nothrow) DepWork();
  if (!k) { c.error = "no-memory"; return ST_BUSY; }
  Status st = ST_REFUSED;
  const JsonDoc& d = *c.doc;
  const int a = c.req->args;
  int t;
  if (!isObject(d, a)) { c.error = "bad-args"; st = ST_INVALID; goto done; }
  t = jsonFind(d, a, "authority_set");
  if (t < 0 || !getSig(d, a, "authority_set_sig", k->set_sig)) { c.error = "bad-args"; st = ST_INVALID; goto done; }
  if (!jsonGetString(d, t, k->set_text, sizeof(k->set_text), CS_ANY, 1)) { c.error = "bad_authority_set"; goto done; }
  k->set_len = strlen(k->set_text);
  t = jsonFind(d, a, "rotations");
  if (t >= 0) {
    if (!isArray(d, t) || d.tok[t].size > Dep::MAX_ROTATIONS) { c.error = "untrusted_authority_key"; goto done; }
    const int n = d.tok[t].size;
    if (n > 0) {
      k->rot_pool = static_cast<char*>(malloc((size_t)n * ROT_TEXT_MAX));
      if (!k->rot_pool) { c.error = "no-memory"; st = ST_BUSY; goto done; }
    }
    for (int i = 0; i < n; i++) {
      const int e = jsonElem(d, t, i);
      char* text = k->rot_pool + (size_t)i * ROT_TEXT_MAX;
      const int tt = jsonFind(d, e, "rotation");
      if (!isObject(d, e) || tt < 0 || !jsonGetString(d, tt, text, ROT_TEXT_MAX, CS_ANY, 1) ||
          !getSig(d, e, "sig", k->rot[i].sig)) {
        c.error = "untrusted_authority_key";   // a chain that cannot be read is a chain that does not verify
        goto done;
      }
      k->rot[i].text = text;
      k->rot[i].len = strlen(text);
      k->n_rot = (uint8_t)(i + 1);
    }
  }
  if (!m->readCurrent(k)) { c.error = "storage"; st = ST_FAILED; goto done; }
  {
    const Dep::ProvErr pe = Dep::checkAuthorityUpdate(k->cur, k->rot, k->n_rot, k->set_text, k->set_len, k->set_sig,
                                                      &k->out, &k->set);
    if (pe == Dep::P_DUPLICATE) {
      w.key("duplicate"); w.boolean(true);
      writeFields(w, *m->_dep, k->cur.rec, k->cur.set_text, false);
      st = ST_OK;
      goto done;
    }
    if (pe != Dep::P_OK) {
      m->_dep->auth_rejections++;
      c.error = Dep::provErrCode(pe);
      goto done;
    }
    if (!m->commit(k)) { c.error = "storage"; st = ST_BUSY; goto done; }
    m->_last_update_ms = millis();
    m->_have_update = true;
    if (!m->saveMarksFor(k->set)) { c.error = "marks"; st = ST_BUSY; goto done; }
    if (!m->readCurrent(k)) { c.error = "storage"; st = ST_FAILED; goto done; }
    writeFields(w, *m->_dep, k->cur.rec, k->cur.set_text, false);
    st = ST_OK;
  }
done:
  delete k;
  return st;
}

#endif  // ENABLE_MECON_IFACE
