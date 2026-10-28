#pragma once
// Zeroes secret bytes in a way the compiler may not remove as a dead store (a plain memset right before a delete or a
// return can be).
#include <stddef.h>
#include <stdint.h>

namespace Iface {

inline void wipe(void* p, size_t n) {
  volatile uint8_t* q = static_cast<volatile uint8_t*>(p);
  while (n--) *q++ = 0;
}

}  // namespace Iface
