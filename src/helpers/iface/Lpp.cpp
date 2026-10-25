#include "Lpp.h"
#include <stdio.h>

namespace Iface {

namespace {
struct LppType {
  uint8_t code;
  uint8_t size;
  bool is_signed;
  uint32_t divisor;
  const char* quantity;
  const char* unit;
};
const LppType TYPES[] = {
  {101, 2, false, 1, "luminosity", "lx"},
  {103, 2, true, 10, "temperature", "C"},
  {104, 1, false, 2, "humidity", "%"},
  {115, 2, false, 10, "pressure", "hPa"},
  {116, 2, false, 100, "voltage", "V"},
  {117, 2, false, 1000, "current", "A"},
  {120, 1, false, 1, "percentage", "%"},
  {121, 2, true, 1, "altitude", "m"},
  {128, 2, false, 1, "power", "W"},
  {130, 4, false, 1000, "distance", "m"},
  {131, 4, false, 1000, "energy", "kWh"},
};
const uint8_t LPP_GPS = 136, LPP_GPS_SIZE = 9;

const LppType* typeOf(uint8_t code) {
  for (const LppType& t : TYPES) if (t.code == code) return &t;
  return nullptr;
}
}  // namespace

int decodeLpp(const uint8_t* buf, size_t len, JsonWriter& w) {
  int n = 0;
  size_t i = 0;
  while (i < len) {
    if (i + 2 > len) return -1;
    const uint8_t channel = buf[i], code = buf[i + 1];
    i += 2;
    if (code == LPP_GPS) {
      if (i + LPP_GPS_SIZE > len) return -1;
      i += LPP_GPS_SIZE;
      continue;
    }
    const LppType* t = typeOf(code);
    if (!t || i + t->size > len) return -1;
    uint32_t raw = 0;
    for (uint8_t k = 0; k < t->size; k++) raw = raw << 8 | buf[i + k];   // big-endian
    i += t->size;
    int64_t v = raw;
    if (t->is_signed && (raw & (1u << (8 * t->size - 1)))) v -= (int64_t)1 << (8 * t->size);
    char num[24];
    if (t->divisor == 1) snprintf(num, sizeof(num), "%lld", (long long)v);
    else snprintf(num, sizeof(num), "%.3f", (double)v / t->divisor);
    w.beginObject();
    w.key("channel"); w.unum(channel);
    w.key("quantity"); w.str(t->quantity);
    w.key("value"); w.raw(num);
    w.key("unit"); w.str(t->unit);
    w.endObject();
    n++;
  }
  return n;
}

}  // namespace Iface
