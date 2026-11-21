#pragma once
#include "ConfigSerializer.h"
#include "BatteryLevel.h"

// The battery guard's settings as a group of the prefs file ("guard"), for the Repeater's and the Companion's NodePrefs.
// The values live in the battery level tracker, which classifies with them; this only reads and writes them. A prefs file
// without the group, as every file written before it, leaves the defaults: guard off, 3550 / 3400 mV, margin 100, wake 900.
class BatteryGuardPrefs : public ConfigSerializer {
protected:
  void structure() override {
    BatteryGuard::Cfg& c = batteryLevel().settings();
    def("en", c.enabled);
    def("low", c.low_mv);
    def("crit", c.critical_mv);
    def("margin", c.margin_mv);
    def("wake", c.wake_s);
  }
};
