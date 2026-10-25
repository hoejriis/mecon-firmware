// Simulated board: Linux host process, simulated LoRa radio, filesystem and serial.
#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/KeyValueStore.h>
#include "sim_toa.h"

#ifndef LORA_CR
#define LORA_CR 5
#endif

class SimBoard : public mesh::MainBoard {
public:
  void begin() {}
  void attachDynamicPrefs(KeyValueStore*) {}
  void setInhibitSleep(bool) {}
  void sleep(uint32_t) override {}
  uint16_t getBattMilliVolts() override { return 4000; }
  const char* getManufacturerName() const override { return "Simulated Board"; }
  uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }
  void reboot() override;     // exits with status 75 so the runner can restart the node
  void powerOff() override;   // exits with status 0
};

class SimRTCClock : public mesh::RTCClock {
  uint32_t _base;
  uint64_t _set_at_us;
  bool _init;   // epoch is read from the command line on first use
public:
  SimRTCClock();
  uint32_t getCurrentTime() override;
  void setCurrentTime(uint32_t t) override;
};

// Radio that exchanges raw packets with the ether broker over a unix socket.
class SimRadio : public mesh::Radio {
  struct Rx { uint8_t data[256]; int len; float rssi, snr; };
  struct Impl;
  Impl* _i;
  float _freq, _bw; uint8_t _sf, _cr;
  int8_t _tx_dbm;
  uint32_t _n_recv, _n_sent, _n_err;
public:
  SimRadio();
  void begin() override;
  void loop() override;
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  float packetScore(float snr, int packet_len) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;
  bool isReceiving() override;
  int getNoiseFloor() const override { return -110; }
  float getLastRSSI() const override;
  float getLastSNR() const override;

  // Surface the examples use on the RadioLib wrappers.
  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) { _freq = freq; _bw = bw; _sf = sf; _cr = cr; sendParams(); }
  void sendParams();
  void setTxPower(int8_t dbm) { _tx_dbm = dbm; }
  uint32_t getRngSeed();
  bool setRxBoostedGainMode(bool) { return false; }
  bool getRxBoostedGainMode() const { return false; }
  bool configSideDetectors(const uint8_t[], uint8_t, float) { return false; }
  void powerOff() {}
  uint32_t getPacketsRecv() const { return _n_recv; }
  uint32_t getPacketsRecvErrors() const { return _n_err; }
  uint32_t getPacketsSent() const { return _n_sent; }
  void resetStats() { _n_recv = _n_sent = _n_err = 0; }
  static uint16_t preambleSymbols(uint8_t sf) { return sf <= 8 ? 32 : 16; }

  // Lock-step virtual time (--lockstep): the broker owns the clock. lockstepJoin() connects before setup() and
  // returns the broker's time at the join; lockstepAdvance() reports "done, next due at abs_us" and blocks, reading
  // the socket, until the broker grants a time >= abs_us. Both return the granted (broker) time.
  uint64_t lockstepJoin();
  uint64_t lockstepAdvance(uint64_t abs_us);
private:
  void connectEther();
  bool readEther(bool block);
  bool parseFrames(bool stop_at_grant);
};

extern SimBoard board;
extern SimRadio radio_driver;
extern SimRTCClock rtc_clock;
// Readings for the environment stream, from "<node dir>.sensors": one "<channel> <quantity> <value>" per line
// (temperature, humidity, pressure, voltage), read on every query so a test can change them while the node runs.
class SimSensorManager : public SensorManager {
public:
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
};
extern SimSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();

// The standalone Companion's user interface is compiled in (the real ui-new screens): a recording display and a
// scriptable button stand in for the OLED and the PRG button.
#ifdef DISPLAY_CLASS
#include "sim_display.h"
#endif
