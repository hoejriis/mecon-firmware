// Minimal RTClib shim: only the DateTime surface the examples use.
#pragma once
#include <stdint.h>
#include <time.h>
#include <stdio.h>

class DateTime {
  uint32_t _t;
public:
  DateTime(uint32_t unixtime = 0) : _t(unixtime) {}
  uint32_t unixtime() const { return _t; }
  static void tmOf(uint32_t t, struct tm& out) { time_t tt = t; gmtime_r(&tt, &out); }
  uint16_t year() const { struct tm m; tmOf(_t, m); return m.tm_year + 1900; }
  uint8_t month() const { struct tm m; tmOf(_t, m); return m.tm_mon + 1; }
  uint8_t day() const { struct tm m; tmOf(_t, m); return m.tm_mday; }
  uint8_t hour() const { struct tm m; tmOf(_t, m); return m.tm_hour; }
  uint8_t minute() const { struct tm m; tmOf(_t, m); return m.tm_min; }
  uint8_t second() const { struct tm m; tmOf(_t, m); return m.tm_sec; }
};
