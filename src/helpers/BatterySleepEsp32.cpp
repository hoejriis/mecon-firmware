// The protective sleep's platform part on the ESP32-S3 boards (the Heltec V3 and V4): a timed deep sleep with the radio,
// the FEM and the peripheral rail held off, the user button as a second wake source, and the guard's block in RTC
// memory. Policy and state: BatterySleep.h; the board-independent part: BatterySleepRun.h. None of the rail states or
// currents in deep sleep has been measured on these boards.
#if defined(ESP_PLATFORM) && defined(BATTERY_SLEEP_BOARD) && defined(ENABLE_MECON_IFACE)

#include <Arduino.h>
#include <target.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include "BatterySleepRun.h"
#include "updates/UpdSlot.h"

#ifndef PIN_VEXT_EN_ACTIVE
#define PIN_VEXT_EN_ACTIVE HIGH   // as RefCountedDigitalPin's default
#endif

static RTC_NOINIT_ATTR BatterySleep::State g_state;
#ifdef MECON_BATTERY_TEST_OFFSET
static RTC_NOINIT_ATTR uint32_t g_offset_word;   // the offset in the low half, its complement in the high half
#endif

namespace BatterySleep {

State& rtc() { return g_state; }
void rtcCommit() {}

Wake wakeCause() {
  const esp_reset_reason_t r = esp_reset_reason();
  if (r == ESP_RST_BROWNOUT) return W_BROWNOUT;
  if (r != ESP_RST_DEEPSLEEP) return W_OTHER;
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: return W_TIMER;
    case ESP_SLEEP_WAKEUP_EXT1: return W_BUTTON;
    default: return W_OTHER;
  }
}

bool trialPending() { return Upd::runningState() == Upd::RS_PENDING; }
uint16_t readMv() { return offsetReading(board.getBattMilliVolts()); }
void restoreAdc(uint16_t x1000) {
  if (x1000) board.setAdcMultiplier(x1000 / 1000.0f);
}

void early() {
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) return;
  // Set the level each held pad keeps before letting go of it, so it does not glitch: the radio's NSS stays high (the
  // board's begin() lets go of that hold), the peripheral rail stays off until the board's begin() and the display ask.
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
#ifdef PIN_VEXT_EN
  pinMode(PIN_VEXT_EN, OUTPUT);
  digitalWrite(PIN_VEXT_EN, !PIN_VEXT_EN_ACTIVE);
  gpio_hold_dis((gpio_num_t)PIN_VEXT_EN);
#endif
  gpio_deep_sleep_hold_dis();
}

bool deepSleep(uint32_t secs, bool peripherals_up) {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  // The timer first: a device that cannot be woken by it stays up, with nothing turned off.
  if (!secs || esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL) != ESP_OK) return false;
#ifdef PIN_USER_BTN
  // The user button wakes it too, for a maintenance lease. Optional: the timer alone is enough to recover.
  if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_USER_BTN)) {
    rtc_gpio_pullup_en((gpio_num_t)PIN_USER_BTN);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_USER_BTN);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);   // keeps the pull-up powered
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_USER_BTN, ESP_EXT1_WAKEUP_ANY_LOW);
  }
#endif
  if (peripherals_up) {
#ifdef DISPLAY_CLASS
    display.turnOff();
#endif
    radio_driver.powerOff();   // the SX1262 to sleep with a cold start
    if (sensors.getLocationProvider() != NULL) sensors.getLocationProvider()->stop();
  }
  digitalWrite(P_LORA_NSS, HIGH);   // keeps the radio asleep
  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
#ifdef P_LORA_PA_POWER
  digitalWrite(P_LORA_PA_POWER, LOW);   // the V4's FEM supply off, as its powerOff() does
  rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);
#endif
#ifdef PIN_VEXT_EN
  digitalWrite(PIN_VEXT_EN, !PIN_VEXT_EN_ACTIVE);   // the peripheral rail off, as the board's begin() leaves it
  gpio_hold_en((gpio_num_t)PIN_VEXT_EN);
  gpio_deep_sleep_hold_en();
#endif
  Serial.flush();
  delay(20);
  esp_deep_sleep_start();
  return false;   // not reached
}

#ifdef MECON_BATTERY_TEST_OFFSET
int16_t& testOffset() {
  static int16_t off = 0;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    const uint16_t lo = (uint16_t)(g_offset_word & 0xFFFF), hi = (uint16_t)(g_offset_word >> 16);
    if ((uint16_t)~lo == hi && lo <= 2000) off = (int16_t)lo;
  }
  return off;
}
void testOffsetCommit() {
  const uint16_t lo = (uint16_t)testOffset();
  g_offset_word = ((uint32_t)(uint16_t)~lo << 16) | lo;
}
#endif

}  // namespace BatterySleep

#endif
