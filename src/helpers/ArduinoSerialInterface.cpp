#include "ArduinoSerialInterface.h"
#include "esp32/NativeUsbTx.h"
#include "SerialTxGate.h"

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3

uint32_t ArduinoSerialInterface::_tx_dropped = 0;

void ArduinoSerialInterface::enable() { 
  _isEnabled = true;
  _state = RECV_STATE_IDLE;
}
void ArduinoSerialInterface::disable() {
  _isEnabled = false;
}

bool ArduinoSerialInterface::isConnected() const { 
  return true;   // no way of knowing, so assume yes
}

#if defined(SIM_FREE_HEAP)
bool sim_serial_tx_held();   // the simulator: --tx-hold-file, a host that stops reading for a while (sim_main.cpp)
#endif

bool ArduinoSerialInterface::isWriteBusy() const {
#if defined(SIM_FREE_HEAP)
  if (sim_serial_tx_held()) return true;
#endif
#if defined(MECON_NATIVE_USB_TX) || defined(NRF52_PLATFORM)
  // A frame waits with its sender rather than overrun the ring, which discards when it overruns (NativeUsbTx.h), or, on nRF52,
  // rather than hold the loop until the host reads (SerialTxGate.h). With no host nothing drains, so nothing waits either.
  if (_serial == &Serial && Serial) return Serial.availableForWrite() < (int)(MAX_FRAME_SIZE + 3);
#endif
  return false;
}

size_t ArduinoSerialInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    // frame is too big!
    return 0;
  }

#if defined(NRF52_PLATFORM)
  // A host that holds the port open without reading: drop the frame, whole, rather than stop the loop with it.
  if (_serial == &Serial && Serial && !serialRoomFor(Serial, len + 3, [] { return (uint32_t)millis(); }, [] { yield(); })) {
    _tx_dropped++;
    return 0;
  }
#endif

  uint8_t hdr[3];
  hdr[0] = '>';
  hdr[1] = (len & 0xFF);  // LSB
  hdr[2] = (len >> 8);    // MSB

  _serial->write(hdr, 3);
  return _serial->write(src, len);
}

size_t ArduinoSerialInterface::checkRecvFrame(uint8_t dest[]) {
  nativeUsbTxNudge(millis());
  while (_serial->available()) {
    int c = _serial->read();
    if (c < 0) break;

    switch (_state) {
      case RECV_STATE_IDLE:
        if (c == '<') {
          _state = RECV_STATE_HDR_FOUND;
        }
        break;
      case RECV_STATE_HDR_FOUND:
        _frame_len = (uint8_t)c;   // LSB
        _state = RECV_STATE_LEN1_FOUND;
        break;
      case RECV_STATE_LEN1_FOUND:
        _frame_len |= ((uint16_t)c) << 8;   // MSB
        rx_len = 0;
        _state = _frame_len > 0 ? RECV_STATE_LEN2_FOUND : RECV_STATE_IDLE;
        break;
      default:
        if (rx_len < MAX_FRAME_SIZE) {
          rx_buf[rx_len] = (uint8_t)c;   // rest of frame will be discarded if > MAX
        }
        rx_len++;
        if (rx_len >= _frame_len) {  // received a complete frame?
          if (_frame_len > MAX_FRAME_SIZE) _frame_len = MAX_FRAME_SIZE;    // truncate
          memcpy(dest, rx_buf, _frame_len);
          _state = RECV_STATE_IDLE;  // reset state, for next frame
          return _frame_len;
        }
    }
  }
  return 0;
}
