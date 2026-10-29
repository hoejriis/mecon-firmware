// The Deployment authority layer of the device interface (docs/contract/AUTHORITY.md), shared by the Companion and the
// Repeater and Room server console: the record and its sequence marks loaded at boot, signed requests checked and
// replay-protected before they run, the channel nonces, and the deployment operations. The checks themselves are the pure
// helpers in src/helpers/deployment/; this file is the glue to storage, the interface core and the CLI privilege table.
// Fail closed throughout: anything that cannot be decided is a refusal, and nothing here prints or returns a secret.
#include "DeployAuth.h"
#include <Arduino.h>
#include <helpers/CryptoRNG.h>
#include <helpers/Posture.h>
#include <helpers/cli/CliCaller.h>
#include <helpers/deployment/DepHmac.h>
#include <helpers/deployment/DepMarker.h>
#include <helpers/deployment/DepProvision.h>
#include <helpers/deployment/DepReplay.h>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace Iface {

namespace {

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
struct DeployAuth::DepWork {
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
bool DeployAuth::readCurrent(DepWork* w) {
  w->cur.state = Dep::LS_ABSENT;
  if (_dep_ls != Dep::LS_OK || !_dep) return true;
  size_t len = 0;
  bool exists = false;
  if (!_env.store->read(_dep->slot, w->frame, sizeof(w->frame), &len, &exists, Dep::SF_RECORD) || !exists) return false;
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
bool DeployAuth::persistMarks() {
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
  if (!n || !_env.store->write(slot, buf, n, Dep::SF_MARKS)) return false;
  _dep->marks_slot = slot;
  _dep->marks_generation = gen;
  _dep->marks_dirty = false;
  return true;
}

// The marks for a newly committed set, written after the record (AUTHORITY.md section 4): a member still present at its
// admission keeps its mark, a re-admitted one restarts at 0, and with the marks lost nobody is trusted.
bool DeployAuth::saveMarksFor(const Dep::AuthoritySet& set) {
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
bool DeployAuth::commit(DepWork* w) {
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
  if (n && _env.store->write(slot, w->frame, n, Dep::SF_RECORD)) {
    size_t len = 0;
    bool exists = false;
    Dep::Record back;
    const char* text;
    uint32_t g;
    ok = _env.store->read(slot, w->frame, sizeof(w->frame), &len, &exists, Dep::SF_RECORD) && exists &&
         Dep::decodeRecord(w->frame, len, &back, &text, &g) && g == gen && back.set_len == w->out.set_len &&
         memcmp(text, w->set_text, back.set_len) == 0;
  }
  if (!ok) {
    if (first) delete d;
    return false;
  }
  // A secret that changed, appeared or went invalidates every challenge and unlock issued against the old one.
  if (first || d->state != Dep::LS_OK || d->rec.has_unlock != w->out.has_unlock ||
      memcmp(d->rec.unlock_secret, w->out.unlock_secret, Dep::UNLOCK_LEN) != 0)
    _secret_epoch++;
  d->state = Dep::LS_OK;
  d->has_salvage = false;
  d->slot = slot;
  d->generation = gen;
  d->other_damaged = false;
  d->rec = w->out;
  d->set = w->set;
  _dep = d;
  _dep_ls = Dep::LS_OK;
  syncCaller();
  return true;
}

// ---- boot

void DeployAuth::begin(const Env& env) {
  _env = env;
  if (_env.publish_caller) {
    Cli::serverCaller().on_format = [](void* u) { static_cast<DeployAuth*>(u)->onFactoryReset(); };
    Cli::serverCaller().on_format_user = this;
    Cli::serverCaller().has_secret = [](void* u) { return static_cast<DeployAuth*>(u)->hasUnlockSecret(); };
    Cli::serverCaller().has_secret_user = this;
  }
  load();
}

// The two facts the servers' command handlers read (Cli::ServerCaller): kept equal to the state in memory.
void DeployAuth::syncCaller() {
  if (!_env.publish_caller) return;
  Cli::serverCaller().enrolled = _dep_ls != Dep::LS_ABSENT;
}

void DeployAuth::load() {
  _secret_epoch++;   // whatever was unlocked or challenged belongs to the state that was just replaced
  loadInner();
  syncCaller();
}

void DeployAuth::loadInner() {
  _dep = nullptr;
  _dep_ls = Dep::LS_ABSENT;
  Dep::CopyStore* st = _env.store;
  // Whether anything exists at all, without reading it: an existing copy that cannot be read is corrupt, never absent.
  bool any = false;
  for (int slot = 0; slot < 2; slot++) {
    size_t len;
    bool exists = false;
    uint8_t probe;
    st->read(slot, &probe, 0, &len, &exists, Dep::SF_RECORD);
    any = any || exists;
  }
  if (!any) {
    // No copy of the record, but the provisioned marker says there was one: corrupt, never absent (section 4).
    _dep_ls = Dep::withMarker(Dep::LS_ABSENT, Dep::markerRead());
    return;
  }
  uint8_t* buf[2] = { static_cast<uint8_t*>(malloc(Dep::MAX_FRAME)), static_cast<uint8_t*>(malloc(Dep::MAX_FRAME)) };
  uint8_t* mbuf[2] = { static_cast<uint8_t*>(malloc(Dep::MARKS_FRAME)), static_cast<uint8_t*>(malloc(Dep::MARKS_FRAME)) };
  if (!buf[0] || !buf[1] || !mbuf[0] || !mbuf[1]) {
    _dep_ls = Dep::LS_CORRUPT;   // cannot check: never absent
  } else {
    Dep::Copy c[2];
    for (int slot = 0; slot < 2; slot++) {
      size_t len = 0;
      bool exists = false;
      const bool ok = st->read(slot, buf[slot], Dep::MAX_FRAME, &len, &exists, Dep::SF_RECORD);
      c[slot].data = exists ? buf[slot] : nullptr;
      c[slot].len = ok ? len : 0;
    }
    _dep_ls = Dep::depLoad(c[0], c[1], &_dep);
    if (_dep_ls == Dep::LS_OK) {
      Dep::Copy m[2];
      for (int slot = 0; slot < 2; slot++) {
        size_t len = 0;
        bool exists = false;
        const bool ok = st->read(slot, mbuf[slot], Dep::MARKS_FRAME, &len, &exists, Dep::SF_MARKS);
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
  // A record that loads but has no marker (provisioned before the marker existed, or the marker was lost) gets it back.
  if (_dep_ls == Dep::LS_OK && Dep::markerRead() == Dep::MK_ABSENT) Dep::markerSet();
}

// What the interface must decide before the privilege table (AUTHORITY.md sections 6 and 9).
DeployAuth::Pre DeployAuth::preAuthorize(const OpEntry& op, const char** deny) {
  *deny = nullptr;
  // Open to any caller that can reach the interface: the read of the stored set (it reveals nothing secret), and the
  // DAK-signed authority update (it carries its own proof; sections 6.2 and 6.3).
  if (strcmp(op.name, "deployment.get") == 0 || strcmp(op.name, "deployment.authority_update") == 0) return PRE_ALLOW;
  // The unlock handshake is for the unsigned cable, and its handlers decide the transport, the secret and the lockout.
  if (strcmp(op.name, "deployment.unlock_challenge") == 0 || strcmp(op.name, "deployment.unlock") == 0) return PRE_ALLOW;
  // Once a device holds a record, a transport is not authority: an unsigned request may read, and nothing more. A first
  // or repeated provisioning is decided by its handler (it knows the transport and the stored record).
  // Likewise the reset, whose handler knows the record's state, the transport and the posture (section 11).
  // identity.rotate likewise: rotateGate() answers with the contract's code for each state. job.ack is of the read class
  // but releases a result, so it is not a plain read (section 7): unsigned, it would let any link discard what is owed.
  const bool plain_read = op.priv == PRIV_READ && strcmp(op.name, "job.ack") != 0;
  if (_dep_ls != Dep::LS_ABSENT && !_signed && !plain_read && strcmp(op.name, "deployment.provision") != 0 &&
      strcmp(op.name, "deployment.reset") != 0 && strcmp(op.name, "identity.rotate") != 0) {
    *deny = _dep_ls == Dep::LS_OK ? "unauthenticated" : "deployment_record_corrupt";
    if (_dep) _dep->auth_rejections++;
    return PRE_DENY;
  }
  return PRE_TABLE;
}

void DeployAuth::writeStatus(JsonWriter& w, bool hardened) { Dep::depWriteStatus(w, _dep_ls, _dep, hardened); }

const char* DeployAuth::helloNonce(const char* link_nonce) const {
  if (_dep_ls == Dep::LS_ABSENT || !link_nonce || !link_nonce[0]) return nullptr;
  return link_nonce;
}

// ---- channel nonces

void DeployAuth::issueNonce(char slot[33]) {
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

bool DeployAuth::isWrapper(const char* m, size_t n) {
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

int DeployAuth::dispatchSigned(const Cli::Context& uctx, char nonce_slot[33], const char* msg, size_t len,
                               ResultCache* cache, RunFn run, void* run_user, char* out, size_t cap) {
  if (_dep_ls == Dep::LS_ABSENT) return writeResult(out, cap, "", ST_REFUSED, nullptr, "unauthenticated");
  if (_dep_ls != Dep::LS_OK || !_dep) {
    if (_dep) _dep->auth_rejections++;
    return writeResult(out, cap, "", ST_REFUSED, nullptr, "deployment_record_corrupt");
  }
  char* body = static_cast<char*>(malloc(Dep::MAX_WRAPPED + 1));
  if (!body) return writeResult(out, cap, "", ST_BUSY, nullptr, "no-memory");
  Dep::Verified v;
  memset(&v, 0, sizeof(v));
  const char* nonce = nonce_slot;
  const Dep::WrapErr e = Dep::verifyWrapped(msg, len, _dep->rec, _dep->set, _env.node_key, Dep::contractOpClass,
                                            nonce[0] ? nonce : nullptr, body, Dep::MAX_WRAPPED + 1, &v);
  int n;
  if (e != Dep::WR_OK) {
    _dep->auth_rejections++;
    n = writeResult(out, cap, "", ST_REFUSED, nullptr, Dep::wrapErrCode(e));
    free(body);
    return n;
  }
  strncpy(_answering.op, v.op, sizeof(_answering.op) - 1);   // the verified request: its result names it
  _answering.op[sizeof(_answering.op) - 1] = 0;
  _answering.has_job = v.has_job;
  _answering.job = v.job_id;
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
      if (cache && cache->find(key, &data, &dl) == ResultCache::HIT && dl < cap) {
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
  if (_env.publish_caller) Cli::serverCaller().signed_line = true;
  n = run(run_user, sctx, v.body, v.body_len, out, cap);   // job ids guard replay; no id cache
  if (_env.publish_caller) Cli::serverCaller().signed_line = false;
  _signed = false;
  if (n > 0 && key[0] && cache) cache->store(key, out, (size_t)n);
  if (v.cls == Dep::OPC_SENSITIVE) issueNonce(nonce_slot);   // used once
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
  w.key("marks_fallback"); w.boolean(s.marks_fallback);
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

Status DeployAuth::opGet(OpContext& c) {
  JsonWriter& w = *c.out;
  if (_dep_ls == Dep::LS_ABSENT) { w.key("state"); w.str("none"); return ST_OK; }
  if (_dep_ls != Dep::LS_OK || !_dep) { w.key("state"); w.str("corrupt"); return ST_OK; }
  DepWork* k = new (std::nothrow) DepWork();
  if (!k) { c.error = "no-memory"; return ST_BUSY; }
  Status st = ST_OK;
  if (readCurrent(k)) writeFields(w, *_dep, k->cur.rec, k->cur.set_text, true);
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

bool noProfiles(void*, uint8_t) { return false; }   // a role with no MQTT profiles: a broker role names none

}  // namespace

// Whether the request being handled arrived over the USB cable (signed or not): the local unlock secret is accepted from
// nowhere else.
bool DeployAuth::viaCable() const { return _ctx.transport == Cli::T_USB && !_ctx.remote; }

Status DeployAuth::opProvision(OpContext& c) {
  JsonWriter& w = *c.out;
  if (_dep_ls == Dep::LS_CORRUPT) {   // never reopen a first provisioning over something that cannot be read
    if (_dep) _dep->auth_rejections++;
    c.error = "deployment_record_corrupt";
    return ST_REFUSED;
  }
  const bool has = _dep_ls == Dep::LS_OK;
  const Cli::Context& cx = _ctx;
  // Unsigned first provisioning: trust on first use, accepted only from the USB cable. BLE never counts, whatever its PIN:
  // the device cannot tell a PIN it drew for the session from one compiled into the image or chosen by the user.
  const bool present = cx.transport == Cli::T_USB && cx.auth == Cli::A_CABLE;
  if (!has && !_signed && !present) { c.error = "unauthenticated"; return ST_REFUSED; }
  // A first provisioning needs the provisioned marker, outside the file system (section 4). A platform with nowhere to keep
  // it cannot tell a device that was never provisioned from one whose files were lost, so it never provisions unsigned.
  if (!has && Dep::markerRead() == Dep::MK_UNSUPPORTED) { c.error = "unsupported"; return ST_REFUSED; }

  DepWork* k = new (std::nothrow) DepWork();
  if (!k) { c.error = "no-memory"; return ST_BUSY; }
  Status st = ST_REFUSED;
  const ProvParsed pp = parseProvision(*c.doc, c.req->args, &k->p, k->name, sizeof(k->name), k->set_text,
                                       sizeof(k->set_text), &k->set_len, k->set_sig);
  if (pp.malformed) { c.error = "bad-args"; st = ST_INVALID; goto done; }
  if (pp.err) { c.error = pp.err; goto done; }
  if (k->p.has_unlock && !viaCable()) { c.error = "usb_only"; goto done; }   // the secret is in the clear in the body
  if (!readCurrent(k)) { c.error = "storage"; st = ST_FAILED; goto done; }
  {
    Dep::ProvisionContext pc = { &k->cur, _env.node_key, _env.profile_exists ? _env.profile_exists : noProfiles, _env.user };
    const Dep::ProvErr pe = Dep::checkProvision(k->p, pc, &k->out, &k->set);
    if (has && !_signed) {
      // Held a record, so only a signed request changes it. An unsigned byte-identical resend gets the read-back; anything
      // else says nothing about why (no oracle for an unauthenticated caller).
      if (pe != Dep::P_DUPLICATE) {
        if (_dep) _dep->auth_rejections++;
        c.error = "unauthenticated";
        goto done;
      }
    }
    if (pe == Dep::P_DUPLICATE) {
      w.key("duplicate"); w.boolean(true);
      writeFields(w, *_dep, k->cur.rec, k->cur.set_text, false);
      st = ST_OK;
      goto done;
    }
    if (pe != Dep::P_OK) {
      if (_dep) _dep->auth_rejections++;
      c.error = Dep::provErrCode(pe);
      goto done;
    }
    // Commit: the marker first on a first provisioning (a failure after it leaves the device reading as corrupt, which the
    // cable can reset; a failure before it must not leave a record with no marker), then the record, then its marks, then
    // the answer read back from storage.
    if (!has && !Dep::markerSet()) { c.error = "storage"; st = ST_BUSY; goto done; }
    if (!commit(k)) {
      if (!has) Dep::markerClear();   // nothing was written: back to a device with no Deployment (if this fails, it reads corrupt)
      c.error = "storage";
      st = ST_BUSY;
      goto done;
    }
    if (!saveMarksFor(k->set)) { c.error = "marks"; st = ST_BUSY; goto done; }
    if (!readCurrent(k)) { c.error = "storage"; st = ST_FAILED; goto done; }
    writeFields(w, *_dep, k->cur.rec, k->cur.set_text, false);
    st = ST_OK;
  }
done:
  delete k;
  return st;
}

Status DeployAuth::opUpdate(OpContext& c) {
  JsonWriter& w = *c.out;
  if (_dep_ls == Dep::LS_CORRUPT) {
    if (_dep) _dep->auth_rejections++;
    c.error = "deployment_record_corrupt";
    return ST_REFUSED;
  }
  if (_dep_ls != Dep::LS_OK) { c.error = "untrusted_authority_key"; return ST_REFUSED; }   // nothing pinned to verify against
  if (_have_update && (uint32_t)(millis() - _last_update_ms) < 1000) {   // flash wear
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
  if (!readCurrent(k)) { c.error = "storage"; st = ST_FAILED; goto done; }
  {
    const Dep::ProvErr pe = Dep::checkAuthorityUpdate(k->cur, k->rot, k->n_rot, k->set_text, k->set_len, k->set_sig,
                                                      &k->out, &k->set);
    if (pe == Dep::P_DUPLICATE) {
      w.key("duplicate"); w.boolean(true);
      writeFields(w, *_dep, k->cur.rec, k->cur.set_text, false);
      st = ST_OK;
      goto done;
    }
    if (pe != Dep::P_OK) {
      _dep->auth_rejections++;
      c.error = Dep::provErrCode(pe);
      goto done;
    }
    if (!commit(k)) { c.error = "storage"; st = ST_BUSY; goto done; }
    _last_update_ms = millis();
    _have_update = true;
    if (!saveMarksFor(k->set)) { c.error = "marks"; st = ST_BUSY; goto done; }
    if (!readCurrent(k)) { c.error = "storage"; st = ST_FAILED; goto done; }
    writeFields(w, *_dep, k->cur.rec, k->cur.set_text, false);
    st = ST_OK;
  }
done:
  delete k;
  return st;
}

// ---- local unlock (AUTHORITY.md section 10)

// The secret the cable's challenge is answered with: the record's, or for a record that is corrupt only because its stored
// set no longer verifies, the one its CRC-intact frame still carries. Null when there is none: fail closed.
const uint8_t* DeployAuth::unlockSecret() const {
  if (!_dep) return nullptr;
  if (_dep_ls == Dep::LS_OK) return _dep->rec.has_unlock ? _dep->rec.unlock_secret : nullptr;
  if (_dep_ls == Dep::LS_CORRUPT && _dep->state == Dep::LS_CORRUPT && _dep->has_salvage) return _dep->salvage;
  return nullptr;
}

bool DeployAuth::unlocked(const LinkUnlock* l) const {
  if (!l || !l->unlocked || _dep_ls == Dep::LS_ABSENT || l->unlocked_epoch != _secret_epoch) return false;
  return (uint32_t)(millis() - l->unlocked_ms) < UNLOCK_MS;
}

void DeployAuth::stampContext(Cli::Context& c, const LinkUnlock* l) const {
  c.unlocked = c.transport == Cli::T_USB && c.auth == Cli::A_CABLE && !c.remote && unlocked(l);
}

bool DeployAuth::lockedOut() {
  if (_locked && (uint32_t)(millis() - _locked_ms) >= LOCKOUT_MS) { _locked = false; _fails = 0; }
  return _locked;
}

// One audit entry per attempt, whatever its outcome; never the response, the challenge or the secret.
void DeployAuth::auditUnlock(uint8_t event) {
  const uint32_t now = _env.now_s ? _env.now_s(_env.user) : millis() / 1000;
  Posture::audit().add(now, event, Cli::auditCaller(Cli::principalOf(_ctx)), Posture::W_UNLOCK);
}

// What stops the handshake before it starts, as an error code; null when it may go on. Only the unsigned cable link may
// try (a signed request, a paired phone, the broker and the radio never), and only while a secret exists and the device
// is not in its lockout.
const char* DeployAuth::unlockGate() {
  const bool cable = !_signed && _link && _ctx.transport == Cli::T_USB && _ctx.auth == Cli::A_CABLE && !_ctx.remote &&
                     Cli::principalOf(_ctx) == Cli::P_CABLE;
  if (!cable) return "usb_only";
  if (!unlockSecret()) return "no_unlock_secret";
  if (lockedOut()) return "locked_out";
  return nullptr;
}

Status DeployAuth::opUnlockChallenge(OpContext& c) {
  const char* why = unlockGate();
  if (why) { auditUnlock(Posture::EV_REFUSED); c.error = why; return ST_REFUSED; }
  LinkUnlock& l = *_link;
  uint8_t b[32];
  memset(b, 0, sizeof(b));
  CryptoRNG rng;   // no fallback: a predictable challenge would be worse than none
  rng.random(b, sizeof(b));
  bool nonzero = false;
  for (uint8_t x : b) nonzero = nonzero || x != 0;
  if (!nonzero) { auditUnlock(Posture::EV_REFUSED); c.error = "rng"; return ST_BUSY; }
  memcpy(l.challenge, b, sizeof(b));   // one outstanding challenge per link: a new one replaces the old
  memset(b, 0, sizeof(b));
  l.have_challenge = true;
  l.challenge_ms = millis();
  l.challenge_epoch = _secret_epoch;
  char hex[2 * 32 + 1];
  hexTo(hex, l.challenge, sizeof(l.challenge));
  JsonWriter& w = *c.out;
  w.key("challenge"); w.str(hex);
  w.key("expires_in_s"); w.unum(CHALLENGE_MS / 1000);
  return ST_OK;
}

Status DeployAuth::opUnlock(OpContext& c) {
  const char* why = unlockGate();
  if (why) { auditUnlock(Posture::EV_REFUSED); c.error = why; return ST_REFUSED; }
  LinkUnlock& l = *_link;
  const uint8_t* secret = unlockSecret();   // non-null: the gate checked
  uint8_t resp[Dep::HMAC_LEN];
  memset(resp, 0, sizeof(resp));
  const bool parsed = isObject(*c.doc, c.req->args) && getHex(*c.doc, c.req->args, "response", resp, sizeof(resp));
  const bool fresh = l.have_challenge && l.challenge_epoch == _secret_epoch &&
                     (uint32_t)(millis() - l.challenge_ms) < CHALLENGE_MS;
  uint8_t expect[Dep::HMAC_LEN];
  Dep::hmacSha256(secret, Dep::UNLOCK_LEN, l.challenge, sizeof(l.challenge), expect);   // always computed: no early out
  const bool match = Dep::constTimeEqual(expect, resp, sizeof(expect));
  memset(expect, 0, sizeof(expect));
  memset(resp, 0, sizeof(resp));
  // A challenge answers one attempt, right or wrong: it is gone before the verdict.
  memset(l.challenge, 0, sizeof(l.challenge)); l.have_challenge = false;
  if (parsed && fresh && match) {
    l.unlocked = true;
    l.unlocked_ms = millis();
    l.unlocked_epoch = _secret_epoch;
    _fails = 0;
    auditUnlock(Posture::EV_UNLOCK);
    JsonWriter& w = *c.out;
    w.key("unlocked"); w.boolean(true);
    w.key("expires_in_s"); w.unum(UNLOCK_MS / 1000);
    return ST_OK;
  }
  l.unlocked = false;   // a failed attempt does not leave an older unlock standing
  if (++_fails >= MAX_FAILURES) { _locked = true; _locked_ms = millis(); }
  auditUnlock(Posture::EV_LOGIN_FAILED);
  c.error = !parsed ? "bad-args" : !fresh ? "no_challenge" : "bad_response";
  return ST_REFUSED;
}

// deployment.set_unlock {"unlock_secret": <64 hex>}: signed only (sensitive, so it also carries the link's nonce). The
// record is rewritten with the new secret and everything else as it was; the reply says that it is set, never what.
Status DeployAuth::opSetUnlock(OpContext& c) {
  if (_dep_ls == Dep::LS_ABSENT) { c.error = "not_enrolled"; return ST_REFUSED; }
  if (!_signed) {
    if (_dep) _dep->auth_rejections++;
    c.error = _dep_ls == Dep::LS_OK ? "unauthenticated" : "deployment_record_corrupt";
    return ST_REFUSED;
  }
  if (_dep_ls != Dep::LS_OK || !_dep) { c.error = "deployment_record_corrupt"; return ST_REFUSED; }
  // The secret travels in the clear inside the request body: only over the cable, never a broker, BLE, TCP or LoRa.
  if (!viaCable()) { c.error = "usb_only"; return ST_REFUSED; }
  uint8_t secret[Dep::UNLOCK_LEN];
  bool zero = true;
  if (!isObject(*c.doc, c.req->args) || !getHex(*c.doc, c.req->args, "unlock_secret", secret, sizeof(secret))) {
    c.error = "bad-args";
    return ST_INVALID;
  }
  for (uint8_t x : secret) zero = zero && x == 0;
  if (zero) { memset(secret, 0, sizeof(secret)); c.error = "bad-args"; return ST_INVALID; }   // not a secret
  DepWork* k = new (std::nothrow) DepWork();
  if (!k) { memset(secret, 0, sizeof(secret)); c.error = "no-memory"; return ST_BUSY; }
  Status st = ST_FAILED;
  if (!readCurrent(k)) { c.error = "storage"; goto done; }
  k->out = k->cur.rec;
  k->out.has_unlock = true;
  memcpy(k->out.unlock_secret, secret, sizeof(secret));
  memcpy(k->set_text, k->cur.set_text, k->out.set_len);   // commit() reuses the frame the text points into
  k->set_text[k->out.set_len] = 0;
  k->set = _dep->set;
  if (!commit(k)) { c.error = "storage"; st = ST_BUSY; goto done; }
  c.out->key("unlock"); c.out->str("set");
  st = ST_OK;
done:
  memset(secret, 0, sizeof(secret));
  delete k;
  return st;
}

// identity.rotate may run only for a member's signature, and only while the record is readable: a device that cannot tell
// who its managers are does not get a new owner's key (section 11).
Status DeployAuth::rotateGate(OpContext& c) {
  if (_dep_ls == Dep::LS_ABSENT) { c.error = "not_enrolled"; return ST_REFUSED; }
  if (_dep_ls != Dep::LS_OK || !_dep) { if (_dep) _dep->auth_rejections++; c.error = "deployment_record_corrupt"; return ST_REFUSED; }
  if (!_signed) { _dep->auth_rejections++; c.error = "unauthenticated"; return ST_REFUSED; }
  return ST_OK;
}

// ---- unenrolment: reset, factory reset, and the rescue console's rebuild

namespace {

// Whether any copy of one of the two files exists, without reading it.
bool anyCopy(Dep::CopyStore* st, Dep::StoreFile which) {
  for (int slot = 0; slot < 2; slot++) {
    size_t len;
    bool exists = false;
    uint8_t probe;
    st->read(slot, &probe, 0, &len, &exists, which);
    if (exists) return true;
  }
  return false;
}

}  // namespace

// deployment.reset (AUTHORITY.md section 11): removes the record (older copy first), the marks, the local unlock secret and
// the provisioned marker; keeps the node key, device_id, configuration, contacts and channels. A healthy record: only
// signed, only over the USB cable, only in Normal. A corrupt record: the unsigned USB cable in Normal, and in Hardened the
// cable link that holds a local unlock (section 10). A healthy record in Hardened is not reset by an unlock alone: section
// 11 names a signature for it, and the stricter reading stands.
Status DeployAuth::opReset(OpContext& c) {
  JsonWriter& w = *c.out;
  const Cli::Context& cx = _ctx;
  if (_dep_ls == Dep::LS_ABSENT) { c.error = "not_enrolled"; return ST_REFUSED; }
  const bool corrupt = _dep_ls != Dep::LS_OK;
  const bool usb = cx.transport == Cli::T_USB;
  if (!corrupt) {
    if (!_signed) { if (_dep) _dep->auth_rejections++; c.error = "unauthenticated"; return ST_REFUSED; }
    if (!usb) { _dep->auth_rejections++; c.error = "usb_only"; return ST_REFUSED; }
  } else if (_signed || !usb || cx.auth != Cli::A_CABLE) {
    if (_dep) _dep->auth_rejections++;
    c.error = "deployment_record_corrupt";
    return ST_REFUSED;
  }
  if (Posture::isHardened(cx.posture) && !(corrupt && Cli::unlockManagement(cx))) {
    if (_dep) _dep->auth_rejections++;
    c.error = corrupt ? "unlock_required" : "hardened";
    return ST_REFUSED;
  }
  Dep::CopyStore* st = _env.store;
  const int rec_slot = _dep ? _dep->slot : -1;
  const int marks_slot = _dep ? _dep->marks_slot : -1;
  const bool had_record = anyCopy(st, Dep::SF_RECORD);
  const bool had_marks = anyCopy(st, Dep::SF_MARKS);
  const bool had_unlock = _dep && _dep_ls == Dep::LS_OK && _dep->rec.has_unlock;
  const bool had_marker = Dep::markerRead() == Dep::MK_PRESENT;
  // The record first, the older copy first within it; the marker last, so a step that fails leaves the device reading as
  // enrolled or corrupt, never as a device with no Deployment while part of it remains.
  const bool ok = st->removeBoth(rec_slot, Dep::SF_RECORD) &&
                  st->removeBoth(marks_slot, Dep::SF_MARKS) && Dep::markerClear();
  if (_dep) Dep::depFree(_dep);
  load();   // the state in memory is what is now stored
  if (!ok) { c.error = "storage"; return ST_FAILED; }
  w.key("state"); w.str("none");
  w.key("removed"); w.beginArray();
  if (had_record) w.str("record");
  if (had_marks) w.str("marks");
  if (had_unlock) w.str("unlock_secret");
  if (had_marker) w.str("marker");
  w.endArray();
  w.key("kept"); w.beginArray();
  w.str("node_key"); w.str("device_id"); w.str("configuration"); w.str("contacts"); w.str("channels");
  w.endArray();
  return ST_OK;
}

void DeployAuth::onFactoryReset() {
  Dep::markerClear();   // the format already takes it on ESP32 (NVS erase) and the sim; kept for a platform whose format does not
  _secret_epoch++;
  if (_dep) Dep::depFree(_dep);
  _dep = nullptr;
  _dep_ls = Dep::withMarker(Dep::LS_ABSENT, Dep::markerRead());   // a marker that would not clear reads as corrupt
  syncCaller();
}

// The four stored files and the marker, as the rescue console's rebuild must find them again.
struct DeployAuth::RecordSnapshot {
  uint8_t* data[4];   // record A, record B, marks A, marks B
  size_t len[4];
  bool exists[4];
  Dep::MarkerState marker;
  RecordSnapshot() { memset(this, 0, sizeof(*this)); }
  ~RecordSnapshot() {
    for (uint8_t* p : data) if (p) { memset(p, 0, Dep::MAX_FRAME); free(p); }   // the record holds the unlock secret
  }
};

static Dep::StoreFile whichOf(int i) { return i < 2 ? Dep::SF_RECORD : Dep::SF_MARKS; }
static size_t capOf(int i) { return i < 2 ? Dep::MAX_FRAME : Dep::MARKS_FRAME; }

DeployAuth::RecordSnapshot* DeployAuth::snapshotRecord() {
  RecordSnapshot* s = new (std::nothrow) RecordSnapshot();
  if (!s) return nullptr;
  s->marker = Dep::markerRead();
  for (int i = 0; i < 4; i++) {
    uint8_t* probe = static_cast<uint8_t*>(malloc(capOf(i)));
    if (!probe) { delete s; return nullptr; }
    size_t len = 0;
    bool exists = false;
    const bool ok = _env.store->read(i & 1, probe, capOf(i), &len, &exists, whichOf(i));
    if (exists && !ok) { memset(probe, 0, capOf(i)); free(probe); delete s; return nullptr; }   // exists, cannot be read
    if (!exists) { free(probe); continue; }
    s->data[i] = probe;
    s->len[i] = len;
    s->exists[i] = true;
  }
  return s;
}

bool DeployAuth::restoreMarker(const RecordSnapshot* s) {
  if (!s) return false;
  if (s->marker != Dep::MK_PRESENT || Dep::markerRead() == Dep::MK_PRESENT) return true;
  return Dep::markerSet();
}

bool DeployAuth::restoreRecord(RecordSnapshot* s) {
  if (!s) return false;
  bool ok = true;
  // The marker first: if a copy below fails to come back, the device then reads as corrupt, never as unenrolled.
  if (s->marker == Dep::MK_PRESENT && Dep::markerRead() != Dep::MK_PRESENT) ok = Dep::markerSet() && ok;
  uint8_t* now = static_cast<uint8_t*>(malloc(Dep::MAX_FRAME));
  if (!now) return false;
  for (int i = 0; i < 4; i++) {
    if (!s->exists[i]) continue;
    size_t len = 0;
    bool exists = false;
    const bool same = _env.store->read(i & 1, now, capOf(i), &len, &exists, whichOf(i)) && exists &&
                      len == s->len[i] && memcmp(now, s->data[i], len) == 0;
    if (same) continue;   // a rebuild that did not format leaves them as they were: no flash wear
    ok = _env.store->write(i & 1, s->data[i], s->len[i], whichOf(i)) && ok;
    ok = _env.store->read(i & 1, now, capOf(i), &len, &exists, whichOf(i)) && exists &&
         len == s->len[i] && memcmp(now, s->data[i], len) == 0 && ok;
  }
  memset(now, 0, Dep::MAX_FRAME);
  free(now);
  if (_dep) Dep::depFree(_dep);
  load();   // the state in memory is what is now stored
  return ok;
}

void DeployAuth::freeSnapshot(RecordSnapshot* s) { delete s; }

}  // namespace Iface
