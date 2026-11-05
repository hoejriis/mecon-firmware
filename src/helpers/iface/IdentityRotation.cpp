#include "IdentityRotation.h"
#include <stdio.h>
#include <string.h>
#include <helpers/CryptoRNG.h>
#include "Provenance.h"
#include "Wipe.h"

namespace Iface {

namespace {
// Replays 32 bytes already drawn and checked, so the key is built from exactly the seed that was tested for zeros.
class SeedRng : public mesh::RNG {
public:
  explicit SeedRng(const uint8_t* s) { memcpy(_s, s, sizeof(_s)); }
  ~SeedRng() { wipe(_s, sizeof(_s)); }
  void random(uint8_t* dest, size_t sz) override {
    for (size_t i = 0; i < sz; i++) dest[i] = i < sizeof(_s) ? _s[i] : 0;
  }
private:
  uint8_t _s[32];
};
}  // namespace

namespace {
const char ROT_DOMAIN[] = "MECON-NODEROT-v1";   // + 0x00 + the statement text, signed with the key being retired
// The stored file: the statement text, '\n', the base64 signature, and a third line "P" while the drop is pending. The
// flag is outside the signed text because the retired key is gone by the time it is cleared.
const size_t ROT_FILE_MAX = 512;

void hex32(char* out, const uint8_t* k) {
  static const char H[] = "0123456789abcdef";
  for (int i = 0; i < PUB_KEY_SIZE; i++) { out[2 * i] = H[k[i] >> 4]; out[2 * i + 1] = H[k[i] & 15]; }
  out[2 * PUB_KEY_SIZE] = 0;
}

size_t rotFile(char* out, size_t cap, const char* text, const char* sig_b64, bool pending) {
  const int n = snprintf(out, cap, "%s\n%s\n%s", text, sig_b64, pending ? "P" : "");
  return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

// Splits a stored file: false when it is not whole. text and sig_b64 are copied out NUL terminated.
bool rotSplit(char* file, size_t n, const char** text, const char** sig_b64, bool* pending) {
  file[n] = 0;
  char* nl = strchr(file, '\n');
  if (!nl) return false;
  *nl = 0;
  char* sig = nl + 1;
  char* nl2 = strchr(sig, '\n');
  if (!nl2 || nl2 - sig != 88) return false;
  *nl2 = 0;
  *text = file;
  *sig_b64 = sig;
  *pending = nl2[1] == 'P';
  return true;
}
}  // namespace

bool rotationRead(const RotationStore& st, char* text, size_t tcap, char* sig_b64, size_t scap) {
  char file[ROT_FILE_MAX + 1];
  const size_t n = st.load(st.user, reinterpret_cast<uint8_t*>(file), ROT_FILE_MAX);
  const char *t, *s;
  bool pending;
  if (!n || !rotSplit(file, n, &t, &s, &pending) || strlen(t) >= tcap || strlen(s) >= scap) return false;
  strcpy(text, t);
  strcpy(sig_b64, s);
  return true;
}

void rotationRecover(const RotationStore& st, const mesh::LocalIdentity& self) {
  char file[ROT_FILE_MAX + 1];
  const size_t n = st.load(st.user, reinterpret_cast<uint8_t*>(file), ROT_FILE_MAX);
  if (!n) return;
  const char *text, *sig;
  bool pending;
  if (!rotSplit(file, n, &text, &sig, &pending)) { st.remove(st.user); return; }
  char cur[2 * PUB_KEY_SIZE + 1];
  hex32(cur, self.pub_key);
  char want[2 * PUB_KEY_SIZE + 16];
  snprintf(want, sizeof(want), "\"new\":\"%s\"", cur);
  if (!strstr(text, want)) { st.remove(st.user); return; }   // the key change never took effect
  if (!pending) return;
  if (!st.drop(st.user)) return;   // stays pending: tried again at the next boot
  char out[ROT_FILE_MAX + 1];
  const size_t m = rotFile(out, sizeof(out), text, sig, false);
  if (m) st.save(st.user, reinterpret_cast<uint8_t*>(out), m);
}

int rotationRun(const RotationStore& st, mesh::LocalIdentity& self_id, const char* device_id_hex, uint32_t job_id, const char** err) {
  // 1. Generate. The cryptographic RNG (AUTHORITY.md section 12); a source that delivers nothing is a refusal, never a
  // fixed key. The reserved id hashes and the current key are redrawn.
  CryptoRNG crng;
  mesh::LocalIdentity fresh;
  bool ok = false;
  uint8_t prev[32] = {0};
  for (int tries = 0; tries < 10 && !ok; tries++) {
    uint8_t seed[32];
    memset(seed, 0, sizeof(seed));
    crng.random(seed, sizeof(seed));
    bool nonzero = false;
    for (uint8_t b : seed) nonzero = nonzero || b != 0;
    // A source stuck at a constant or a short pattern passes the zero test: a draw with fewer than 16 distinct byte values, or
    // one equal to the previous draw, is a failing source too (a random 32 bytes has about 28 distinct values).
    bool seen[256];
    memset(seen, 0, sizeof(seen));
    int distinct = 0;
    for (uint8_t b : seed) { if (!seen[b]) { seen[b] = true; distinct++; } }
    if (!nonzero || distinct < 16 || (tries > 0 && memcmp(seed, prev, sizeof(seed)) == 0)) { wipe(seed, sizeof(seed)); wipe(prev, sizeof(prev)); wipe(&fresh, sizeof(fresh)); *err = "rng"; return 0; }
    memcpy(prev, seed, sizeof(prev));
    SeedRng r(seed);
    wipe(seed, sizeof(seed));
    fresh = mesh::LocalIdentity(&r);
    ok = fresh.pub_key[0] != 0x00 && fresh.pub_key[0] != 0xFF && !fresh.matches(self_id);
  }
  wipe(prev, sizeof(prev));
  if (!ok) { *err = "rng"; wipe(&fresh, sizeof(fresh)); return 0; }
  // 2. The rotation statement: the old key signs "old key is replaced by new key" before anything changes, and it is
  // stored (marked drop-pending) before the key is. A manager that lost the result reads it back (identity.rotation).
  {
    char oldhex[2 * PUB_KEY_SIZE + 1], newhex[2 * PUB_KEY_SIZE + 1];
    hex32(oldhex, self_id.pub_key);
    hex32(newhex, fresh.pub_key);
    char msg[sizeof(ROT_DOMAIN) + 256];
    memcpy(msg, ROT_DOMAIN, sizeof(ROT_DOMAIN));   // with its NUL
    char* text = msg + sizeof(ROT_DOMAIN);
    const int tl = snprintf(text, 256, "{\"v\":1,\"old\":\"%s\",\"new\":\"%s\",\"device_id\":\"%s\",\"job_id\":%lu}", oldhex,
                            newhex, device_id_hex, (unsigned long)job_id);
    if (tl <= 0 || tl >= 256) { *err = "failed"; wipe(&fresh, sizeof(fresh)); return 0; }
    uint8_t sig[64];
    self_id.sign(sig, reinterpret_cast<const uint8_t*>(msg), (int)(sizeof(ROT_DOMAIN) + tl));
    char sig_b64[89];
    base64Encode(sig_b64, sig, 64);
    char file[ROT_FILE_MAX + 1];
    const size_t fl = rotFile(file, sizeof(file), text, sig_b64, true);
    if (!fl || !st.save(st.user, reinterpret_cast<uint8_t*>(file), fl)) {
      st.remove(st.user);
      *err = "storage";
      wipe(&fresh, sizeof(fresh));
      return 0;
    }
  }
  // 3. Save the key, atomically. A failed save leaves the old key in place and the device exactly as it was.
  if (!st.save_key(st.user, fresh)) {
    st.remove(st.user);
    wipe(&fresh, sizeof(fresh));
    *err = "storage";
    return 0;
  }
  // 4. Swap.
  self_id = fresh;
  // 5. Drop what belonged to the old key. A cut before this ends is finished at the next boot (the statement is still
  // marked drop-pending).
  const bool dropped = st.drop(st.user);
  if (dropped) {
    char text[256], sig_b64[89];
    char out[ROT_FILE_MAX + 1];
    if (rotationRead(st, text, sizeof(text), sig_b64, sizeof(sig_b64))) {
      const size_t m = rotFile(out, sizeof(out), text, sig_b64, false);
      if (m) st.save(st.user, reinterpret_cast<uint8_t*>(out), m);
    }
  }
  wipe(&fresh, sizeof(fresh));
  return dropped ? 1 : 2;
}

}  // namespace Iface
