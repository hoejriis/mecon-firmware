#pragma once
// Channels over the device interface (docs/contract/OPERATIONS.md section 5.1): channel.list, channel.get,
// channel.put and channel.remove, on the native channel table so there is one store. A channel secret is written,
// never read: results carry the name, the one-byte channel hash and the key size. The table is injected, so the
// logic runs in the native unit tests against a fake one.
#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"

namespace Iface {

const unsigned CHANNEL_NAME_MAX = 31;     // bytes of a name; the native slot holds 32 with the terminator
const unsigned CHANNEL_SECRET_BYTES = 16; // only 128-bit keys, as the native CMD_SET_CHANNEL frame
const unsigned CHANNEL_PAGE = 8;          // used slots per channel.list page

struct ChannelInfo {
  char name[CHANNEL_NAME_MAX + 1];   // "" for an empty slot
  uint8_t hash;                      // the channel hash on air (first byte of SHA-256 of the key)
  uint8_t key_bytes;                 // 16 or 32
};

struct ChannelHost {
  int capacity;                                                            // slots 0 .. capacity - 1
  bool (*get)(void* user, int slot, ChannelInfo& out);                    // false when the slot does not exist
  // Writes the slot and saves the table; an empty name and an all-zero secret clear it. False when it cannot.
  bool (*set)(void* user, int slot, const char* name, const uint8_t secret[CHANNEL_SECRET_BYTES]);
  int (*find)(void* user, const uint8_t secret[CHANNEL_SECRET_BYTES]);    // slot holding this key, or -1
  void* user;
};

// OpContext.user is a ChannelHost.
Status opChannelList(OpContext& c);
Status opChannelGet(OpContext& c);
Status opChannelPut(OpContext& c);
Status opChannelRemove(OpContext& c);

}  // namespace Iface
