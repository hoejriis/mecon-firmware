#pragma once
// Received messages over the device interface (docs/contract/OPERATIONS.md section 6.4): the manager's own copy of what the
// native Companion receives, read with inbox.list and released with inbox.ack. The native offline queue stays the stock
// app's, and neither reader takes anything from the other. RAM only, so a reboot loses it and says so by its epoch.
#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"

namespace Iface {

struct InboxEntry {
  static constexpr size_t TEXT_CAP = 160;   // what a stock sender can send; longer is kept cut, and says so
  uint32_t seq;             // this boot's reception count; 0: a free slot
  uint32_t ts;              // the sender's own claim of the time, never checked
  uint32_t rx_ts;           // this device's clock at reception; 0 while that clock has not been set
  uint32_t rx_ms;           // uptime at reception, for an age that needs no clock
  uint8_t hash[8];          // the native packet hash: the same id any receiver or observer computes
  uint8_t from[32];         // a direct message's stored contact
  uint8_t signer[4];        // a signed plain message: the prefix the native frame carries
  int8_t channel;           // the channel slot; -1 for a direct message
  int8_t snr_x4;            // as the native frame carries it
  uint8_t path_len;         // as the native frame carries it; 0xFF arrived by a direct route
  uint8_t type;             // the native TXT_TYPE_*
  uint8_t len;
  bool cut;
  bool from_manager;        // message.deliver put it here: no radio reception, and nothing checked its sender or its time
  char text[TEXT_CAP + 1];
};

class Inbox {
public:
  static constexpr int SLOTS = 8;
  ~Inbox();
  // The slots are allocated only for a device with a manager, so a plain stock radio keeps no second copy of its messages.
  bool ready() const { return _e != nullptr; }
  bool alloc();
  // Counts a reception and gives its slot: a free one, else the oldest channel message, else the oldest direct one (the
  // native queue's preference). Null with no slots. An entry pushed out, or a reception with no slots, counts as lost.
  InboxEntry* add();
  void ackUpTo(uint32_t seq);   // releases and wipes what the manager has
  // The entry with the lowest seq above `after`, or null.
  const InboxEntry* nextAfter(uint32_t after) const;
  int unread() const;
  uint32_t received() const { return _received; }
  uint32_t acked() const { return _acked; }
  uint32_t lost() const { return _lost; }

  // What arrived lately, by a hash of its sender, time and text (section 6.5), so one message reaches the stock app once
  // whichever way it comes. RAM only: a reboot empties the native queue too.
  static uint32_t identity(const uint8_t* from, int channel, uint32_t ts, const char* text, size_t len);
  bool wasHeard(uint32_t id) const;        // by radio or by delivery
  bool wasDelivered(uint32_t id) const;    // by delivery
  void remember(uint32_t id, bool delivered);

private:
  static constexpr int RECENT = 16;
  InboxEntry* _e = nullptr;
  uint32_t _received = 0, _acked = 0, _lost = 0;
  uint32_t _recent[RECENT] = {};   // the low bit says delivered
  uint8_t _recent_at = 0;
};

enum DeliverResult { DELIVER_OK, DELIVER_DUPLICATE, DELIVER_NOT_FOUND, DELIVER_TOO_LONG, DELIVER_QUEUE_FULL };

struct InboxHost {
  Inbox* inbox;
  uint32_t epoch;
  uint32_t now_ms;
  // Puts one message on the native receive queue as the manager supplied it. Never on the radio.
  DeliverResult (*deliver)(void* user, const uint8_t* from, int channel, uint32_t ts, const char* text, size_t len);
  bool has_channels;
  void* user;
};

// OpContext.user is an InboxHost.
Status opInboxList(OpContext& c);
Status opInboxAck(OpContext& c);
Status opMessageDeliver(OpContext& c);

// An inbox event's data: {"received","unread","lost"}. No sender and no text, so it goes wherever a job event goes.
bool writeInboxEventData(char* out, size_t cap, const Inbox& b);

}  // namespace Iface
