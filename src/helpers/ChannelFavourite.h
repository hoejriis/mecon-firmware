#pragma once
// Which channel messages notify the Companion's screen and buzzer (docs/UI.md, "Notifications").
// A direct message always notifies; a channel message only when its slot is marked favourite. Everything else about a
// received message (storage, the app queue, the push tickle, forwarding) does not depend on this flag.
//
// The mark is kept in the first of the four reserved bytes that open each record of the channels file (written as zero
// by stock firmware, and ignored by it on read). An older file therefore reads as every slot off, and the mark is
// saved and replaced with the channel itself, in the one atomic file write.
#include <stdint.h>
#include <string.h>

namespace ChannelFav {

const uint8_t MARK = 1;   // the value of the first reserved byte of a favourite channel's record

// The reserved bytes of a record as it is written.
inline void encode(bool favourite, uint8_t reserved[4]) {
  memset(reserved, 0, 4);
  if (favourite) reserved[0] = MARK;
}

// A record's reserved bytes as they are read: only the mark itself turns it on.
inline uint8_t decode(const uint8_t reserved[4]) { return reserved[0] == MARK ? 1 : 0; }

// The gate. Asks the mesh for the slot, as BaseChatMesh::getChannel does: a slot outside the table is not found and
// does not notify, and neither does one never marked. D is ChannelDetails.
template <class D, class Mesh>
inline bool notifies(Mesh& mesh, int slot) {
  D ch;
  return mesh.getChannel(slot, ch) && ch.favourite != 0;
}

}  // namespace ChannelFav
