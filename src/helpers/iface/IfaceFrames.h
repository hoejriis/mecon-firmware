#pragma once
// USB/BLE framing (DEVICE_INTERFACE.md section 7): 0x7F | xid | index | count | up to 168 message bytes.
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "IfaceJson.h"   // MAX_MSG
#include "Wipe.h"

namespace Iface {

const uint8_t FRAME_CODE = 0x7F;
const size_t FRAME_HDR = 4;
const size_t FRAME_PAYLOAD = 168;
const size_t FRAME_MAX = FRAME_HDR + FRAME_PAYLOAD;
const unsigned FRAME_MAX_COUNT = 25;      // 25 * 168 = 4200 >= MAX_MSG
const uint32_t REASSEMBLY_TIMEOUT_MS = 5000;
const int QUEUE_MESSAGES = 8;
const size_t RESULT_RESERVE = 512;        // queue bytes events may not use, so a short result (busy) always fits

// Frames needed for a message of len bytes; 0 when len is 0 or over MAX_MSG.
inline unsigned frameCount(size_t len) {
  return (len == 0 || len > MAX_MSG) ? 0 : (unsigned)((len + FRAME_PAYLOAD - 1) / FRAME_PAYLOAD);
}

// Splits one contiguous message into frames, in order.
class Fragmenter {
public:
  bool begin(const uint8_t* msg, size_t len, uint8_t xid) {
    _n = frameCount(len);
    if (!_n) return false;
    _msg = msg; _len = len; _xid = xid; _i = 0;
    return true;
  }
  bool done() const { return _i >= _n; }
  // Writes the next frame (out needs FRAME_MAX bytes) and returns its length, 0 when done.
  size_t next(uint8_t* out) {
    if (done()) return 0;
    size_t off = (size_t)_i * FRAME_PAYLOAD;
    size_t take = _len - off < FRAME_PAYLOAD ? _len - off : FRAME_PAYLOAD;
    out[0] = FRAME_CODE; out[1] = _xid; out[2] = (uint8_t)_i; out[3] = (uint8_t)_n;
    memcpy(out + FRAME_HDR, _msg + off, take);
    _i++;
    return FRAME_HDR + take;
  }
private:
  const uint8_t* _msg = nullptr; size_t _len = 0; uint8_t _xid = 0; unsigned _i = 0, _n = 0;
};

enum RxResult {
  RX_INCOMPLETE,   // frame accepted (or a duplicate ignored); more needed
  RX_COMPLETE,     // data()/size() hold the whole message until the next feed() or release()
  RX_MALFORMED,    // frame rejected; reassembly state unchanged
  RX_NOMEM         // the buffer for a new message could not be allocated; the frame is dropped (answer busy)
};

// One inbound message at a time. Frames of the message may arrive in any order; every frame but the last
// carries exactly FRAME_PAYLOAD bytes, which is what lets a frame be placed without waiting for its
// neighbours. The buffer (count * FRAME_PAYLOAD bytes, at most about 4.1 KB) is allocated when a message starts
// and freed when it has been taken or dropped, so an idle transport holds none.
class Reassembler {
public:
  Reassembler() { reset(); }
  ~Reassembler() { release(); }
  Reassembler(const Reassembler&) = delete;
  Reassembler& operator=(const Reassembler&) = delete;
  // Frees the buffer of a completed message once the caller is done with data(). Wiped first: a request may carry a
  // secret (a config.apply of a password).
  void release() {
    if (_buf) wipe(_buf, _cap);
    free(_buf); _buf = nullptr; _cap = 0; _total = 0;
  }

  RxResult feed(const uint8_t* f, size_t len, uint32_t now_ms);
  // Discards an incomplete message older than the timeout. True when it did (see takeDiscard).
  bool tick(uint32_t now_ms);
  // Set when an incomplete message was dropped, by timeout or because a new xid arrived; reading clears it.
  bool takeDiscard(uint8_t* xid) { if (!_discarded) return false; *xid = _discarded_xid; _discarded = false; return true; }
  bool active() const { return _active; }
  const uint8_t* data() const { return _buf; }
  size_t size() const { return _total; }

private:
  void reset() { _active = false; _discarded = false; _mask = 0; _total = 0; _last_len = 0; _have_last = false; }
  void drop() { _discarded = true; _discarded_xid = _xid; _active = false; _mask = 0; _have_last = false; release(); }
  uint8_t* _buf = nullptr;
  size_t _cap = 0;
  bool _active, _discarded, _have_last;
  uint8_t _xid, _count, _discarded_xid;
  uint32_t _mask, _start;
  size_t _total, _last_len;
};

// Bounded outbound queue for one transport: at most QUEUE_MESSAGES messages in caller-provided byte storage.
// Frames of one message are handed out contiguously, so messages never interleave.
class OutQueue {
public:
  enum Kind { EVENT, RESULT };
  enum Push { PUSH_OK, PUSH_DROPPED, PUSH_BUSY, PUSH_BAD };
  OutQueue(uint8_t* store, size_t cap) : _s(store), _cap(cap) {}

  // Events may fill at most QUEUE_MESSAGES - 1 slots and must leave RESULT_RESERVE bytes; past that an event is
  // dropped and counted. A result that does not fit gets PUSH_BUSY: the request is answered busy instead, and that
  // short answer always fits in what events had to leave.
  Push push(Kind k, const uint8_t* msg, size_t len);
  // Next frame of the message at the head (out needs FRAME_MAX bytes); 0 when empty.
  size_t nextFrame(uint8_t* out);
  int count() const { return _count; }
  // Whether an event of len bytes would be taken now (the same test push() makes).
  bool eventFits(size_t len) const {
    return _count < QUEUE_MESSAGES - 1 && _cap >= RESULT_RESERVE && _used <= _cap - RESULT_RESERVE &&
           len <= _cap - RESULT_RESERVE - _used;
  }
  // Everything queued is dropped, a message half sent included: the link dropped and its next client starts afresh.
  void clear() { _head = 0; _used = 0; _count = 0; _first = 0; _next_frame = 0; }
  uint32_t droppedEvents() const { return _dropped; }
  uint32_t refusedResults() const { return _refused; }

private:
  void copyOut(size_t from, uint8_t* to, size_t n) const;
  uint8_t* _s; size_t _cap;
  size_t _head = 0, _used = 0;       // ring position of the head message and bytes held
  uint16_t _lens[QUEUE_MESSAGES];
  int _count = 0, _first = 0;        // _first: index into _lens of the head
  unsigned _next_frame = 0;
  uint8_t _xid = 0;
  uint32_t _dropped = 0, _refused = 0;
};

}  // namespace Iface
