// Simulated board: a recording display and a scriptable user button.
//
// SimDisplay is a DisplayDriver that draws nothing; it appends every distinct frame's text to the file named by
// --ui-frames, so a test can assert what the firmware put on the screen (including that no secret was drawn).
// SimButton replays the gestures a test appends to the file named by --ui-input, one word per line:
// click, long, double, triple (the events of MomentaryButton).
#pragma once
#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/MomentaryButton.h>   // BUTTON_EVENT_* only; the class itself is not compiled here
#include <string>

class SimDisplay : public DisplayDriver {
  bool _on = false;
  int _size = 1, _x = 0, _y = 0;
  std::string _frame, _last;   // the frame being drawn and the last one written
  unsigned long _frames = 0;
public:
  SimDisplay() : DisplayDriver(128, 64) {}
  bool begin() { return true; }
  bool isOn() override { return _on; }
  void turnOn() override { _on = true; }
  void turnOff() override { _on = false; }
  void clear() override {}
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override { _size = sz; }
  void setColor(ColorVal) override {}
  void setCursor(int x, int y) override { _x = x; _y = y; }
  void print(const char* str) override;
  void fillRect(int, int, int, int) override {}
  void drawRect(int, int, int, int) override {}
  void drawXbm(int, int, const uint8_t*, int, int) override {}
  uint16_t getTextWidth(const char* str) override { return (uint16_t)(strlen(str) * 6 * _size); }
  void endFrame() override;
};

class SimButton {
  size_t _off = 0;
  bool _started = false;
  unsigned long _next_poll = 0;
public:
  void begin() {}
  int check(bool repeat_click = false);   // one BUTTON_EVENT_* per call
  bool isPressed() const { return false; }
};

extern SimDisplay display;
extern SimButton user_btn;
