#include "IfaceFrames.h"

namespace Iface {

bool Reassembler::tick(uint32_t now_ms) {
  if (_active && (uint32_t)(now_ms - _start) >= REASSEMBLY_TIMEOUT_MS) { drop(); return true; }
  return false;
}

RxResult Reassembler::feed(const uint8_t* f, size_t len, uint32_t now_ms) {
  if (!f || len < FRAME_HDR + 1 || len > FRAME_MAX || f[0] != FRAME_CODE) return RX_MALFORMED;
  uint8_t xid = f[1], index = f[2], count = f[3];
  size_t pl = len - FRAME_HDR;
  if (count < 1 || count > FRAME_MAX_COUNT || index >= count) return RX_MALFORMED;
  bool last = (index == count - 1);
  if (!last && pl != FRAME_PAYLOAD) return RX_MALFORMED;
  if (last && (size_t)(count - 1) * FRAME_PAYLOAD + pl > MAX_MSG) return RX_MALFORMED;

  tick(now_ms);                         // a stale partial must not absorb this frame
  if (_active && xid == _xid && count != _count) return RX_MALFORMED;
  if (_active && xid != _xid) drop();   // one message at a time: the newer one wins
  if (!_active) {
    release();                          // a completed message the caller did not release
    _buf = (uint8_t*)malloc((size_t)count * FRAME_PAYLOAD);
    if (!_buf) return RX_NOMEM;
    _cap = (size_t)count * FRAME_PAYLOAD;
    _active = true; _xid = xid; _count = count; _start = now_ms; _mask = 0; _have_last = false; _total = 0;
  }
  uint32_t bit = (uint32_t)1 << index;
  if (_mask & bit) return RX_INCOMPLETE;   // a duplicate frame changes nothing
  memcpy(_buf + (size_t)index * FRAME_PAYLOAD, f + FRAME_HDR, pl);
  _mask |= bit;
  if (last) { _have_last = true; _last_len = pl; }
  if (_mask == (((uint32_t)1 << count) - 1)) {
    _total = (size_t)(count - 1) * FRAME_PAYLOAD + _last_len;
    _active = false;
    return RX_COMPLETE;
  }
  return RX_INCOMPLETE;
}

OutQueue::Push OutQueue::push(Kind k, const uint8_t* msg, size_t len) {
  if (!msg || !frameCount(len)) return PUSH_BAD;
  if (k == EVENT) {
    if (_count >= QUEUE_MESSAGES - 1 || _cap < RESULT_RESERVE || _used > _cap - RESULT_RESERVE || len > _cap - RESULT_RESERVE - _used) {
      _dropped++;
      return PUSH_DROPPED;
    }
  } else if (_count >= QUEUE_MESSAGES || len > _cap - _used) {
    _refused++;
    return PUSH_BUSY;
  }
  size_t at = (_head + _used) % _cap;
  for (size_t i = 0; i < len; i++) _s[(at + i) % _cap] = msg[i];
  _lens[(_first + _count) % QUEUE_MESSAGES] = (uint16_t)len;
  _count++;
  _used += len;
  return PUSH_OK;
}

void OutQueue::copyOut(size_t from, uint8_t* to, size_t n) const {
  for (size_t i = 0; i < n; i++) to[i] = _s[(_head + from + i) % _cap];
}

size_t OutQueue::nextFrame(uint8_t* out) {
  if (_count == 0) return 0;
  size_t len = _lens[_first];
  unsigned n = frameCount(len);
  size_t off = (size_t)_next_frame * FRAME_PAYLOAD;
  size_t take = len - off < FRAME_PAYLOAD ? len - off : FRAME_PAYLOAD;
  out[0] = FRAME_CODE; out[1] = _xid; out[2] = (uint8_t)_next_frame; out[3] = (uint8_t)n;
  copyOut(off, out + FRAME_HDR, take);
  if (++_next_frame >= n) {            // message fully handed out: free it, number the next one
    _head = (_head + len) % _cap; _used -= len;
    _first = (_first + 1) % QUEUE_MESSAGES; _count--;
    _next_frame = 0; _xid++;
  }
  return FRAME_HDR + take;
}

}  // namespace Iface
