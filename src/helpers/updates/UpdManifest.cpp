#include "UpdManifest.h"
#include <new>
#include <stdlib.h>
#include <string.h>
#include "helpers/deployment/DepVerify.h"
#include "helpers/iface/IfaceJson.h"

namespace Upd {

using namespace Iface;

namespace {

typedef JsonBuf<64> ManifestDoc;   // 19 keys and their values, the image object and the revision list

// Every key of the object at t is one of names[], and none appears twice.
bool onlyKeys(const JsonDoc& d, int t, const char* const* names, int n_names) {
  if (!isObject(d, t)) return false;
  uint32_t seen = 0;
  int k = t + 1;
  for (int i = 0; i < d.tok[t].size; i++) {
    int match = -1;
    for (int j = 0; j < n_names; j++) {
      const size_t nl = strlen(names[j]);
      if (d.tok[k].type == JSMN_STRING && (size_t)d.tlen(k) == nl && memcmp(d.str(k), names[j], nl) == 0) {
        match = j;
        break;
      }
    }
    if (match < 0 || (seen & (1u << match))) return false;
    seen |= 1u << match;
    k = jsonSkip(d, k + 1);   // past the value
  }
  return true;
}

int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;   // lowercase only, as the contract writes key ids and digests
}

bool getHexLower(const JsonDoc& d, int obj, const char* key, uint8_t* out, size_t n) {
  char buf[65];
  if (2 * n + 1 > sizeof(buf)) return false;
  const int t = jsonFind(d, obj, key);
  if (t < 0 || !jsonGetString(d, t, buf, 2 * n + 1, CS_NAME, 2 * n)) return false;
  for (size_t i = 0; i < n; i++) {
    const int hi = hexv(buf[2 * i]), lo = hexv(buf[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

bool getStr(const JsonDoc& d, int obj, const char* key, char* out, size_t cap, Charset cs) {
  return jsonGetString(d, jsonFind(d, obj, key), out, cap, cs, 1);
}

bool getU32(const JsonDoc& d, int obj, const char* key, uint32_t lo, uint32_t hi, uint32_t* out) {
  int64_t v;
  if (!jsonGetInt(d, jsonFind(d, obj, key), lo, hi, &v)) return false;
  *out = (uint32_t)v;
  return true;
}

const char* const KEYS[] = {"v", "key_id", "line", "version", "build", "upstream", "interface", "hardware",
                            "hardware_rev", "role", "variant", "layout", "bootloader_min", "data_format",
                            "security_version", "image", "kind", "min_from", "downgrade", "issued_at"};
const char* const IMAGE_KEYS[] = {"size", "sha256"};

bool parseDoc(const JsonDoc& d, Manifest* m) {
  if (!onlyKeys(d, 0, KEYS, sizeof(KEYS) / sizeof(KEYS[0]))) return false;
  uint32_t v;
  if (!getU32(d, 0, "v", 1, 1, &v)) return false;
  if (!getHexLower(d, 0, "key_id", m->key_id, sizeof(m->key_id))) return false;
  if (!getStr(d, 0, "line", m->line, sizeof(m->line), CS_NAME)) return false;
  if (!getStr(d, 0, "version", m->version, sizeof(m->version), CS_ANY)) return false;
  if (!getStr(d, 0, "min_from", m->min_from, sizeof(m->min_from), CS_ANY)) return false;
  if (!getStr(d, 0, "build", m->build, sizeof(m->build), CS_NAME)) return false;
  if (!getStr(d, 0, "upstream", m->upstream, sizeof(m->upstream), CS_ANY)) return false;
  if (!getStr(d, 0, "interface", m->interface_, sizeof(m->interface_), CS_NAME)) return false;
  if (!getStr(d, 0, "hardware", m->hardware, sizeof(m->hardware), CS_ANY)) return false;
  if (!getStr(d, 0, "role", m->role, sizeof(m->role), CS_NAME)) return false;
  if (!getStr(d, 0, "variant", m->variant, sizeof(m->variant), CS_NAME)) return false;

  const int hr = jsonFind(d, 0, "hardware_rev");
  if (!isArray(d, hr) || d.tok[hr].size < 1 || d.tok[hr].size > MAX_HW_REVS) return false;
  m->n_hardware_rev = (uint8_t)d.tok[hr].size;
  for (int i = 0; i < m->n_hardware_rev; i++)
    if (!jsonGetString(d, jsonElem(d, hr, i), m->hardware_rev[i], sizeof(m->hardware_rev[i]), CS_ID, 1)) return false;

  if (!getU32(d, 0, "layout", 1, 0xFFFF, &m->layout)) return false;
  const int bm = jsonFind(d, 0, "bootloader_min");
  if (bm < 0) return false;
  m->has_bootloader_min = !jsonIsNull(d, bm);
  if (m->has_bootloader_min && !getU32(d, 0, "bootloader_min", 0, 0xFFFFFF, &m->bootloader_min)) return false;
  if (!getU32(d, 0, "data_format", 1, 0xFFFF, &m->data_format)) return false;
  if (!getU32(d, 0, "security_version", 0, 0xFFFF, &m->security_version)) return false;

  const int im = jsonFind(d, 0, "image");
  if (!onlyKeys(d, im, IMAGE_KEYS, 2)) return false;
  if (!getU32(d, im, "size", 1, 16u * 1024 * 1024, &m->image_size)) return false;
  if (!getHexLower(d, im, "sha256", m->image_sha256, sizeof(m->image_sha256))) return false;

  char kind[16];
  if (!getStr(d, 0, "kind", kind, sizeof(kind), CS_NAME)) return false;
  if (strcmp(kind, "app") == 0) m->kind = KIND_APP;
  else if (strcmp(kind, "bootloader-dfu") == 0) m->kind = KIND_BOOTLOADER_DFU;
  else return false;
  if (!jsonGetBool(d, jsonFind(d, 0, "downgrade"), &m->downgrade)) return false;
  if (!getU32(d, 0, "issued_at", 0, 0xFFFFFFFFu, &m->issued_at)) return false;
  return true;
}

}  // namespace

bool parseManifest(const char* text, size_t len, Manifest* out) {
  if (!text || !out || len == 0 || len > MANIFEST_MAX) return false;
  ManifestDoc* d = new (std::nothrow) ManifestDoc();
  if (!d) return false;
  memset(out, 0, sizeof(*out));
  const bool ok = jsonParse(*d, text, len) == J_OK && parseDoc(*d, out);
  delete d;
  return ok;
}

const Anchor* findAnchor(const uint8_t key_id[8]) {
  for (size_t i = 0; i < PRODUCTION_ANCHORS_COUNT; i++)
    if (memcmp(PRODUCTION_ANCHORS[i].key_id, key_id, 8) == 0) return &PRODUCTION_ANCHORS[i];
  for (size_t i = 0; i < TEST_ANCHORS_COUNT; i++)
    if (memcmp(TEST_ANCHORS[i].key_id, key_id, 8) == 0) return &TEST_ANCHORS[i];
  return nullptr;
}

bool verifyManifest(const char* text, size_t len, const uint8_t sig[SIG_LEN], const uint8_t key[32]) {
  static const char DOMAIN[] = "MECON-RELEASE-v1";
  const size_t dl = sizeof(DOMAIN) - 1;
  if (!text || len == 0 || len > MANIFEST_MAX) return false;
  uint8_t* msg = static_cast<uint8_t*>(malloc(dl + 1 + len));
  if (!msg) return false;
  memcpy(msg, DOMAIN, dl);
  msg[dl] = 0;
  memcpy(msg + dl + 1, text, len);
  const bool ok = Dep::verifySig(sig, msg, dl + 1 + len, key);
  free(msg);
  return ok;
}

bool parseVersion(const char* s, Version* out) {
  if (!s || !out) return false;
  const char* p = strstr(s, "-mecon.");
  p = p ? p + 7 : s;
  if (*p == 'v') p++;
  memset(out, 0, sizeof(*out));
  for (int i = 0; i < 3; i++) {
    if (*p < '0' || *p > '9') return false;
    uint32_t n = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      if (++digits > 6) return false;
      n = n * 10 + (uint32_t)(*p++ - '0');
    }
    out->n[i] = n;
    if (i < 2 && *p++ != '.') return false;
  }
  if (*p == '-') {   // a pre-release, up to any "+"
    out->pre = true;
    size_t k = 0;
    p++;
    while (*p && *p != '+' && k + 1 < sizeof(out->pre_tag)) out->pre_tag[k++] = *p++;
    if (*p && *p != '+') return false;   // longer than this build keeps: not ordered
  }
  return *p == 0 || *p == '+';   // a development suffix ("+5.gabc", "+dirty") does not order
}

int compareVersion(const Version& a, const Version& b) {
  for (int i = 0; i < 3; i++)
    if (a.n[i] != b.n[i]) return a.n[i] < b.n[i] ? -1 : 1;
  if (a.pre != b.pre) return a.pre ? -1 : 1;   // a release above its pre-releases
  if (!a.pre) return 0;
  const int c = strcmp(a.pre_tag, b.pre_tag);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

}  // namespace Upd
