#pragma once
// The one channel slot of a role with no channel table (Repeater, Room server), for its management channel
// (docs/contract/MANAGEMENT_CHANNEL.md section 6): a name and a 128-bit key in a small file of its own, replaced atomically
// and checked with a CRC. It carries no channel messaging. Seen through the same ChannelHost as a Companion's native
// table, so the channel.* and mgmt.channel.* operations are the same code. Header-only; not built in the native tests.
#include <string.h>
#include <Utils.h>
#include "ChannelOps.h"
#include "ConfigSnap.h"
#include "Wipe.h"

namespace Iface {

class MgmtSlot {
public:
  explicit MgmtSlot(SnapStore* store = nullptr) : _store(store) {}
  void setStore(SnapStore* store) { _store = store; }
  ChannelHost host() { return ChannelHost{_store ? 1 : 0, hostGet, hostSet, hostFind, this}; }

  // False when no key is stored: an absent or damaged file reads as an empty slot, so a binding to it is stale.
  bool read(char name[CHANNEL_NAME_MAX + 1], uint8_t key[CHANNEL_SECRET_BYTES]) const {
    uint8_t img[IMAGE];
    const bool ok = _store && _store->load(img, sizeof(img)) == sizeof(img) && memcmp(img, MAGIC, 4) == 0 &&
                    crcOf(img, IMAGE - 4) == get32(img + IMAGE - 4);
    if (ok) {
      memcpy(name, img + 4, CHANNEL_NAME_MAX);
      name[CHANNEL_NAME_MAX] = 0;
      memcpy(key, img + 4 + CHANNEL_NAME_MAX + 1, CHANNEL_SECRET_BYTES);
    }
    wipe(img, sizeof(img));
    return ok && !isZero(key);
  }

private:
  static constexpr size_t IMAGE = 4 + CHANNEL_NAME_MAX + 1 + CHANNEL_SECRET_BYTES + 4;
  static constexpr const char* MAGIC = "MSL1";

  static uint32_t get32(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
  static uint32_t crcOf(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
      c ^= p[i];
      for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
  }
  static bool isZero(const uint8_t* key) {
    uint8_t any = 0;
    for (unsigned i = 0; i < CHANNEL_SECRET_BYTES; i++) any |= key[i];
    return any == 0;
  }
  static bool hostGet(void* user, int slot, ChannelInfo& out) {
    if (slot != 0) return false;
    uint8_t key[CHANNEL_SECRET_BYTES];
    memset(&out, 0, sizeof(out));
    if (static_cast<MgmtSlot*>(user)->read(out.name, key)) {
      out.keyed = true;
      out.key_bytes = CHANNEL_SECRET_BYTES;
      mesh::Utils::sha256(&out.hash, 1, key, CHANNEL_SECRET_BYTES);   // the channel hash on air, as a native channel's
    }
    wipe(key, sizeof(key));
    return true;
  }
  static bool hostSet(void* user, int slot, const char* name, const uint8_t secret[CHANNEL_SECRET_BYTES]) {
    MgmtSlot* s = static_cast<MgmtSlot*>(user);
    if (slot != 0 || !s->_store) return false;
    if (!name[0] && isZero(secret)) {   // how a slot is cleared
      s->_store->discard();
      return true;
    }
    uint8_t img[IMAGE];
    memset(img, 0, sizeof(img));
    memcpy(img, MAGIC, 4);
    strncpy(reinterpret_cast<char*>(img + 4), name, CHANNEL_NAME_MAX);
    memcpy(img + 4 + CHANNEL_NAME_MAX + 1, secret, CHANNEL_SECRET_BYTES);
    const uint32_t c = crcOf(img, IMAGE - 4);
    for (int i = 0; i < 4; i++) img[IMAGE - 4 + i] = (uint8_t)(c >> (8 * i));
    const bool ok = s->_store->save(img, sizeof(img));
    wipe(img, sizeof(img));
    return ok;
  }
  static int hostFind(void* user, const uint8_t secret[CHANNEL_SECRET_BYTES]) {
    char name[CHANNEL_NAME_MAX + 1];
    uint8_t key[CHANNEL_SECRET_BYTES];
    const bool same = static_cast<MgmtSlot*>(user)->read(name, key) && memcmp(key, secret, CHANNEL_SECRET_BYTES) == 0;
    wipe(key, sizeof(key));
    return same ? 0 : -1;
  }

  SnapStore* _store;
};

}  // namespace Iface
