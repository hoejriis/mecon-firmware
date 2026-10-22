#include "Dispatcher.h"

#if MESH_PACKET_LOGGING
  #include <Arduino.h>
#endif

#include <math.h>
#include <stdio.h>
#include <helpers/RegulatoryDutyCycle.h>

namespace mesh {

#define MAX_RX_DELAY_MILLIS        32000  // 32 seconds

#ifndef NOISE_FLOOR_CALIB_INTERVAL
  #define NOISE_FLOOR_CALIB_INTERVAL   2000     // 2 seconds
#endif

void Dispatcher::begin() {
  n_sent_flood = n_sent_direct = 0;
  n_recv_flood = n_recv_direct = 0;
  _err_flags = 0;
  radio_nonrx_start = _ms->getMillis();

  // A per-node phase, so nodes that boot together do not share bin edges (unsigned arithmetic: wrap-safe).
  duty.reset(_ms->getMillis() - (getDutyPhaseMs() % DUTY_BIN_MS));

  _radio->begin();
  prev_isrecv_mode = _radio->isInRecvMode();
}

float Dispatcher::getAirtimeBudgetFactor() const {
  return 1.0;
}

// ---- duty-cycle window ---------------------------------------------------------------------------------

// Seeding happens on first use rather than in begin(), because the roles learn their channel, and so their limit,
// only after begin() (see DutyWindow::seed()).
void Dispatcher::dutyAdvance(unsigned long now) {
  if (!duty.seeded()) duty.seed(getDutyLimitMs());
  duty.advance(now);
}

void Dispatcher::dutyRecord(unsigned long now, uint32_t air_ms) {
  dutyAdvance(now);
  duty.record(now, air_ms);
}

uint32_t Dispatcher::getTrailingAirtimeMs() {
  dutyAdvance(_ms->getMillis());
  return duty.sum();
}

// The stricter of what the user asked for (airtime factor af means a duty of 1/(1+af)) and the regulatory
// floor for the channel the radio is tuned to.  The floor does not depend on the CLI's 0-9 clamp on af.
uint32_t Dispatcher::getDutyLimitBp() const {
  float af = getAirtimeBudgetFactor();
  if (!(af > 0.0f)) af = 0.0f;                        // also covers NaN
  float user = 10000.0f / (1.0f + af);
  uint32_t limit = user < 1.0f ? 1 : (uint32_t)user;  // rounds down: never more than the user asked for
  float freq, bw;
  if (getRadioChannel(freq, bw)) {
    uint32_t floor_bp = regulatory::dutyFloor(freq, bw).limit_bp;
    if (floor_bp < limit) limit = floor_bp;
  }
  return limit;
}

uint32_t Dispatcher::getDutyLimitMs() const {
  return (uint32_t)((uint64_t)DUTY_WINDOW_MS * getDutyLimitBp() / 10000);
}

unsigned long Dispatcher::getRemainingTxBudget() {
  uint32_t used = getTrailingAirtimeMs(), limit = getDutyLimitMs();
  return used >= limit ? 0 : limit - used;
}

static void fmtBp(char* out, uint32_t bp) {   // 1000 -> "10.00%"
  sprintf(out, "%u.%02u%%", (unsigned)(bp / 100), (unsigned)(bp % 100));
}

void Dispatcher::formatDutyCycleReply(char* reply) {
  uint32_t used = getTrailingAirtimeMs();
  uint32_t limit = getDutyLimitBp();
  float af = getAirtimeBudgetFactor();
  if (!(af > 0.0f)) af = 0.0f;
  uint32_t user = (uint32_t)(10000.0f / (1.0f + af));
  if (user < 1) user = 1;

  char band[28], lim[12], flo[12], usr[12], pct[12];
  strcpy(band, "none");
  strcpy(flo, "none");
  float freq, bw;
  if (getRadioChannel(freq, bw)) {
    regulatory::DutyFloor f = regulatory::dutyFloor(freq, bw);
    if (f.limit_bp < REG_DUTY_NONE_BP) {
      sprintf(band, "%u.%03u-%u.%03u MHz", (unsigned)(f.band_lo_khz / 1000), (unsigned)(f.band_lo_khz % 1000),
              (unsigned)(f.band_hi_khz / 1000), (unsigned)(f.band_hi_khz % 1000));
      fmtBp(flo, f.limit_bp);
    }
  }
  fmtBp(lim, limit);
  fmtBp(usr, user);
  fmtBp(pct, (uint32_t)((uint64_t)used * 10000 / DUTY_WINDOW_MS));
  sprintf(reply, "> band %s, limit %s, floor %s, user %s, used %u ms (%s)", band, lim, flo, usr, (unsigned)used, pct);
}

int Dispatcher::calcRxDelay(float score, uint32_t air_time) const {
  return (int) ((pow(10, 0.85f - score) - 1.0) * air_time);
}

uint32_t Dispatcher::getCADFailRetryDelay() const {
  return 200;
}
uint32_t Dispatcher::getCADFailMaxDuration() const {
  return 4000;   // 4 seconds
}

void Dispatcher::loop() {
  if (millisHasNowPassed(next_floor_calib_time)) {
    _radio->triggerNoiseFloorCalibrate(getInterferenceThreshold());
    _radio->setCADEnabled(getCADEnabled());
    next_floor_calib_time = futureMillis(NOISE_FLOOR_CALIB_INTERVAL);
  }
  _radio->loop();

  // check for radio 'stuck' in mode other than Rx
  bool is_recv = _radio->isInRecvMode();
  if (is_recv != prev_isrecv_mode) {
    prev_isrecv_mode = is_recv;
    if (!is_recv) {
      radio_nonrx_start = _ms->getMillis();
    }
  }
  if (!is_recv && _ms->getMillis() - radio_nonrx_start > 8000) {   // radio has not been in Rx mode for 8 seconds!
    _err_flags |= ERR_EVENT_STARTRX_TIMEOUT;
  }

  if (outbound) {  // waiting for outbound send to be completed
    if (_radio->isSendComplete()) {
      long t = _ms->getMillis() - outbound_start;
      total_air_time += t;
      //Serial.print("  airtime="); Serial.println(t);

      // Charge the larger of the estimated time on air and the time the send actually took, the latter
      // capped at the send timeout (1.5 x estimate) so a stalled loop cannot book hours of airtime.
      uint32_t air = outbound_est_ms;
      uint32_t cap = outbound_est_ms + outbound_est_ms / 2;
      uint32_t took = t < 0 ? 0 : (uint32_t)t;
      if (took > cap) took = cap;
      if (took > air) air = took;
      dutyRecord(_ms->getMillis(), air);

      next_tx_time = _ms->getMillis();

      _radio->onSendFinished();
      logTx(outbound, 2 + outbound->getPathByteLen() + outbound->payload_len);
      if (outbound->isRouteFlood()) {
        n_sent_flood++;
      } else {
        n_sent_direct++;
      }
      releasePacket(outbound);  // return to pool
      outbound = NULL;
    } else if (millisHasNowPassed(outbound_expiry)) {
      MESH_DEBUG_PRINTLN("%s Dispatcher::loop(): WARNING: outbound packed send timed out!", getLogDateTime());

      _radio->onSendFinished();
      logTxFail(outbound, 2 + outbound->getPathByteLen() + outbound->payload_len);

      releasePacket(outbound);  // return to pool
      outbound = NULL;
    } else {
      return;  // can't do any more radio activity until send is complete or timed out
    }

    // going back into receive mode now...
    next_agc_reset_time = futureMillis(getAGCResetInterval());
  }

  if (getAGCResetInterval() > 0 && millisHasNowPassed(next_agc_reset_time)) {
    _radio->resetAGC();
    next_agc_reset_time = futureMillis(getAGCResetInterval());
  }

  // check inbound (delayed) queue
  {
    Packet* pkt = _mgr->getNextInbound(_ms->getMillis());
    if (pkt) {
      processRecvPacket(pkt);
    }
  }
  checkRecv();
  checkSend();
}

bool Dispatcher::tryParsePacket(Packet* pkt, const uint8_t* raw, int len) {
  int i = 0;

  pkt->header = raw[i++];
  if (pkt->getPayloadVer() > PAYLOAD_VER_1) {
    MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): unsupported packet version", getLogDateTime());
    return false;
  }

  if (pkt->hasTransportCodes()) {
    memcpy(&pkt->transport_codes[0], &raw[i], 2); i += 2;
    memcpy(&pkt->transport_codes[1], &raw[i], 2); i += 2;
  } else {
    pkt->transport_codes[0] = pkt->transport_codes[1] = 0;
  }

  pkt->path_len = raw[i++];
  uint8_t path_mode = pkt->path_len >> 6;  // upper 2 bits (legacy firmware: 00)
  if (path_mode == 3) {   // Reserved for future
    MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): unsupported path mode: 3", getLogDateTime());
    return false;
  }

  uint8_t path_byte_len = (pkt->path_len & 63) * pkt->getPathHashSize();
  if (path_byte_len > MAX_PATH_SIZE || i + path_byte_len > len) {
    MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): partial or corrupt packet received, len=%d", getLogDateTime(), len);
    return false;
  }

  memcpy(pkt->path, &raw[i], path_byte_len); i += path_byte_len;

  pkt->payload_len = len - i;  // payload is remainder
  if (pkt->payload_len > sizeof(pkt->payload)) {
    MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): packet payload too big, payload_len=%d", getLogDateTime(), (uint32_t)pkt->payload_len);
    return false;
  }

  memcpy(pkt->payload, &raw[i], pkt->payload_len);

  return true;  // success
}

void Dispatcher::checkRecv() {
  Packet* pkt;
  float score;
  uint32_t air_time;
  {
    uint8_t raw[MAX_TRANS_UNIT+1];
    int len = _radio->recvRaw(raw, MAX_TRANS_UNIT);
    if (len > 0) {
      logRxRaw(_radio->getLastSNR(), _radio->getLastRSSI(), raw, len);

      pkt = _mgr->allocNew();
      if (pkt == NULL) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): WARNING: received data, no unused packets available!", getLogDateTime());
      } else {
        if (tryParsePacket(pkt, raw, len)) {
          pkt->_snr = _radio->getLastSNR() * 4.0f;
          score = _radio->packetScore(_radio->getLastSNR(), len);
          air_time = _radio->getEstAirtimeFor(len);
          rx_air_time += air_time;
        } else {
          _mgr->free(pkt);  // put back into pool
          pkt = NULL;
        }
      }
    } else {
      pkt = NULL;
    }
  }
  if (pkt) {
    #if MESH_PACKET_LOGGING
    Serial.print(getLogDateTime());
    Serial.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d time=%d", 
            pkt->getRawLength(), pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
            (int)pkt->getSNR(), (int)_radio->getLastRSSI(), (int)(score*1000), air_time);

    static uint8_t packet_hash[MAX_HASH_SIZE];
    pkt->calculatePacketHash(packet_hash);
    Serial.print(" hash=");
    mesh::Utils::printHex(Serial, packet_hash, MAX_HASH_SIZE);

    if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ
        || pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
      Serial.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
    } else {
      Serial.printf("\n");
    }
    #endif
    logRx(pkt, pkt->getRawLength(), score);   // hook for custom logging

    if (pkt->isRouteFlood()) {
      n_recv_flood++;

      int _delay = calcRxDelay(score, air_time);
      if (_delay < 50) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(), score delay below threshold (%d)", getLogDateTime(), _delay);
        processRecvPacket(pkt);   // is below the score delay threshold, so process immediately
      } else {
        MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(), score delay is: %d millis", getLogDateTime(), _delay);
        if (_delay > MAX_RX_DELAY_MILLIS) {
          _delay = MAX_RX_DELAY_MILLIS;
        }
        _mgr->queueInbound(pkt, futureMillis(_delay)); // add to delayed inbound queue
      }
    } else {
      n_recv_direct++;
      processRecvPacket(pkt);
    }
  }
}

void Dispatcher::processRecvPacket(Packet* pkt) {
  DispatcherAction action = onRecvPacket(pkt);
  if (action == ACTION_RELEASE) {
    _mgr->free(pkt);
  } else if (action == ACTION_MANUAL_HOLD) {
    // sub-class is wanting to manually hold Packet instance, and call releasePacket() at appropriate time
  } else {   // ACTION_RETRANSMIT*
    uint8_t priority = (action >> 24) - 1;
    uint32_t _delay = action & 0xFFFFFF;

    _mgr->queueOutbound(pkt, priority, futureMillis(_delay));
  }
}

void Dispatcher::checkSend() {
  if (_mgr->getOutboundCount(_ms->getMillis()) == 0) return;
  
  if (!millisHasNowPassed(next_tx_time)) return;
  if (_radio->isReceiving()) {
    if (cad_busy_start == 0) {
      cad_busy_start = _ms->getMillis();   // record when CAD busy state started
    }

    if (_ms->getMillis() - cad_busy_start > getCADFailMaxDuration()) {
      _err_flags |= ERR_EVENT_CAD_TIMEOUT;

      MESH_DEBUG_PRINTLN("%s Dispatcher::checkSend(): CAD busy max duration reached!", getLogDateTime());
      // channel activity has gone on too long... (Radio might be in a bad state)
      // force the pending transmit below...
    } else {
      next_tx_time = futureMillis(getCADFailRetryDelay());
      return;
    }
  }
  cad_busy_start = 0;  // reset busy state

  outbound = _mgr->getNextOutbound(_ms->getMillis());
  if (outbound) {
    int len = 0;
    uint8_t raw[MAX_TRANS_UNIT];

    raw[len++] = outbound->header;
    if (outbound->hasTransportCodes()) {
      memcpy(&raw[len], &outbound->transport_codes[0], 2); len += 2;
      memcpy(&raw[len], &outbound->transport_codes[1], 2); len += 2;
    }
    raw[len++] = outbound->path_len;
    len += Packet::writePath(&raw[len], outbound->path, outbound->path_len);

    if (len + outbound->payload_len > MAX_TRANS_UNIT) {
      MESH_DEBUG_PRINTLN("%s Dispatcher::checkSend(): FATAL: Invalid packet queued... too long, len=%d", getLogDateTime(), len + outbound->payload_len);
      _mgr->free(outbound);
      outbound = NULL;
    } else {
      memcpy(&raw[len], outbound->payload, outbound->payload_len); len += outbound->payload_len;

      // Duty cycle: send only if the trailing hour plus this packet stays within the limit.  The packet is
      // known only now, so it is put back (as the most urgent, scheduled for when the airtime it needs has
      // aged out) if it does not fit; a packet longer than the whole limit can never be sent and is dropped.
      uint32_t est = _radio->getEstAirtimeFor(len);
      uint32_t limit_ms = getDutyLimitMs();
      if (est > limit_ms) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::checkSend(): packet airtime %u ms exceeds duty-cycle limit %u ms, dropped", getLogDateTime(), (uint32_t)est, (uint32_t)limit_ms);
        logTxFail(outbound, outbound->getRawLength());
        releasePacket(outbound);
        outbound = NULL;
        return;
      }
      unsigned long now = _ms->getMillis();
      dutyAdvance(now);
      if (duty.sum() + est > limit_ms) {
        uint32_t wait = duty.waitMs(now, duty.sum() + est - limit_ms);
        _mgr->queueOutbound(outbound, 0, futureMillis(wait));
        outbound = NULL;
        next_tx_time = futureMillis(wait);
        return;
      }
      outbound_est_ms = est;

      uint32_t max_airtime = est*3/2;
      outbound_start = _ms->getMillis();
      bool success = _radio->startSendRaw(raw, len);
      if (!success) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::loop(): ERROR: send start failed!", getLogDateTime());

        logTxFail(outbound, outbound->getRawLength());
  
        releasePacket(outbound);  // return to pool
        outbound = NULL;
        return;
      }
      outbound_expiry = futureMillis(max_airtime);

    #if MESH_PACKET_LOGGING
      Serial.print(getLogDateTime());
      Serial.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)", 
            len, outbound->getPayloadType(), outbound->isRouteDirect() ? "D" : "F", outbound->payload_len);
      if (outbound->getPayloadType() == PAYLOAD_TYPE_PATH || outbound->getPayloadType() == PAYLOAD_TYPE_REQ
        || outbound->getPayloadType() == PAYLOAD_TYPE_RESPONSE || outbound->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        Serial.printf(" [%02X -> %02X]\n", (uint32_t)outbound->payload[1], (uint32_t)outbound->payload[0]);
      } else {
        Serial.printf("\n");
      }
    #endif
    }
  }
}

Packet* Dispatcher::obtainNewPacket() {
  auto pkt = _mgr->allocNew();  // TODO: zero out all fields
  if (pkt == NULL) {
    _err_flags |= ERR_EVENT_FULL;
  } else {
    pkt->payload_len = pkt->path_len = 0;
    pkt->_snr = 0;
  }
  return pkt;
}

void Dispatcher::releasePacket(Packet* packet) {
  _mgr->free(packet);
}

void Dispatcher::sendPacket(Packet* packet, uint8_t priority, uint32_t delay_millis) {
  if (!Packet::isValidPathLen(packet->path_len) || packet->payload_len > MAX_PACKET_PAYLOAD) {
    MESH_DEBUG_PRINTLN("%s Dispatcher::sendPacket(): ERROR: invalid packet... path_len=%d, payload_len=%d", getLogDateTime(), (uint32_t) packet->path_len, (uint32_t) packet->payload_len);
    _mgr->free(packet);
  } else {
    _mgr->queueOutbound(packet, priority, futureMillis(delay_millis));
  }
}

// Utility function -- handles the case where millis() wraps around back to zero
//   2's complement arithmetic will handle any unsigned subtraction up to HALF the word size (32-bits in this case)
bool Dispatcher::millisHasNowPassed(unsigned long timestamp) const {
  return (long)(_ms->getMillis() - timestamp) > 0;
}

unsigned long Dispatcher::futureMillis(int millis_from_now) const {
  return _ms->getMillis() + millis_from_now;
}

}