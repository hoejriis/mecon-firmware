#pragma once
// Messages over the device interface (docs/contract/OPERATIONS.md section 6): message.send asks the native Companion to
// send one direct or channel message, and message.get, message.list and message.ack read what became of it. The outcome
// table is RAM only and holds no message text. A send is not a job: jobs run one at a time and each change writes flash,
// and a send is routine traffic that can wait seconds for an ACK. The radio is injected, so the logic runs in the native
// unit tests against a fake one.
#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"

namespace Iface {

enum MessageState : uint8_t {
  MSG_QUEUED = 1,     // handed to the radio queue
  MSG_DUTY_HELD,      // in the queue, waiting for airtime
  MSG_TRANSMITTED,    // on the air. Final for a channel message, which has no ACK
  MSG_ACKED,          // the recipient's native ACK arrived. It does not prove an application read it
  MSG_TIMED_OUT,      // no ACK in the native estimate. It does not prove non-receipt, and a late ACK still moves it to acked
  MSG_FAILED,         // the radio dropped it or could not send it
  MSG_UNKNOWN         // left the queue with no report, or (message.get only) not in this table
};

struct MessageEntry {
  static constexpr size_t ID_CAP = 32;
  char id[ID_CAP + 1];     // the request id, or "#<issuer>:<job_id>" for a signed request
  uint32_t seq;            // creation order, for eviction
  uint8_t hash[8];         // the native packet hash: the same id any receiver or observer computes
  uint32_t ack;            // the native ACK code; 0 for a channel message
  uint32_t ts;             // the message's own timestamp
  uint32_t est_ms;         // the native estimate of the round trip, counted from the transmission
  uint32_t since_ms;       // when the current wait began
  uint32_t wait_ms;        // while duty-held: the wait the dispatcher last gave
  uint8_t to[6];           // the recipient's key prefix, for a direct message
  int8_t channel;          // the channel slot; -1 for a direct message
  uint8_t state;           // 0: a free slot
  bool flood;
  bool collected;          // message.ack: a reader has it, so this slot goes first
  bool tell;               // a change no event has told yet
};

class MessageTable {
public:
  static constexpr int SLOTS = 8;
  static constexpr uint32_t QUEUE_LIMIT_MS = 2UL * 3600 * 1000;   // past the longest duty wait: the report went missing

  MessageEntry* find(const char* id);
  // A slot for a new send, evicting the oldest collected entry, then the oldest final one. Null when all are open.
  MessageEntry* claim(const char* id);
  void onTx(const uint8_t hash[8], uint32_t now);
  void onTxFail(const uint8_t hash[8]);
  void onHeld(const uint8_t hash[8], uint32_t wait_ms);
  // Every entry waiting on this ACK code, and a timed-out one too. True when any matched; `to` is then the recipient.
  bool onAck(uint32_t ack, uint8_t to[6]);
  void tick(uint32_t now);
  MessageEntry* nextToTell();   // the next entry with an untold change; clears its mark
  bool anyOpen() const { return open() > 0; }
  int open() const;
  int owed() const;
  const MessageEntry& at(int i) const { return _e[i]; }

private:
  MessageEntry _e[SLOTS] = {};
  uint32_t _seq = 0;
};

inline bool messageFinal(const MessageEntry& e) {
  return e.state >= MSG_ACKED || (e.state == MSG_TRANSMITTED && e.channel >= 0);
}
const char* messageStateName(uint8_t s);

enum MessageSend { MS_OK, MS_NOT_FOUND, MS_TOO_LONG, MS_QUEUE_FULL };

// What the native send reports.
struct MessageSent {
  uint8_t hash[8];
  uint32_t ack;
  uint32_t est_ms;
  bool flood;
};

// The flood scope of one send: the device's default (as a stock app's send), none, or the scope a request names.
struct SendScope {
  enum Mode : uint8_t { DEFAULT, NONE, KEY } mode;
  uint8_t key[16];
};

struct MessageHost {
  MessageTable* table;     // null when the heap cannot hold it
  unsigned text_max;       // the longest text the native send takes
  bool has_channels;
  MessageSend (*send_dm)(void* user, const uint8_t key[32], uint32_t ts, uint8_t attempt, const char* text,
                         const SendScope& scope, MessageSent& out);
  MessageSend (*send_channel)(void* user, int slot, uint32_t ts, const char* text, size_t len, const SendScope& scope,
                              MessageSent& out);
  uint32_t (*clock)(void* user);                                       // the timestamp of a message that names none
  bool (*identity)(void* user, const Request& req, char* out, size_t cap);   // the request's identity (OPERATIONS.md 3.1)
  // A named scope's key, as scope.default.set reads it (ScopeOps' scopeOf); null where the build has no scopes.
  bool (*scope_of)(OpContext& c, int name_tok, int key_tok, char* name, uint8_t* key);
  uint32_t epoch;
  uint32_t now_ms;
  void* user;
};

// OpContext.user is a MessageHost.
Status opMessageSend(OpContext& c);
Status opMessageGet(OpContext& c);
Status opMessageList(OpContext& c);
Status opMessageAck(OpContext& c);

// A message event's data: {"message","hash","state"}. No recipient and no text, so it goes wherever a job event goes.
bool writeMessageEventData(char* out, size_t cap, const MessageEntry& e);

}  // namespace Iface
