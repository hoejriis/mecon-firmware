#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class WioTrackerL1Board : public NRF52BoardDCDC {
protected:
  uint8_t btn_prev_state;

public:
  WioTrackerL1Board() : NRF52Board("WioTrackerL1_OTA") {}
  void begin();

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL);
    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  // USB power from the nRF52 USB regulator's VBUS flag (NRF52Board::isExternalPowered): present or not. It says nothing
  // about a solar or other supply on the battery connector, so it is reported as `usb_power` and the health stream's
  // `power_source`, not as `external` (hasPowerDetect stays false, as no charger pin exists).
  bool hasUsbPowerDetect() const override { return true; }
  bool isUsbPowered() override { return NRF52Board::isExternalPowered(); }

  const char* getManufacturerName() const override {
    return "Seeed Wio Tracker L1";
  }

  void powerOff() override {
    NRF52Board::powerOff();
  }
};
