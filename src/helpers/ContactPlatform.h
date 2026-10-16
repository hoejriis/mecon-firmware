#pragma once

// Board-side inputs of the contact store (docs/CONTACTS.md): the free heap at boot, and the per-board limits.
// Every limit can be set per board with a build flag; the defaults suit a board with 100+ KB of free heap.

#include <stdint.h>
#include "ContactStore.h"

#ifndef CONTACT_CACHE_FLOOR
  #define CONTACT_CACHE_FLOOR 16        // the cache never has fewer slots than this
#endif
#ifndef CONTACT_CACHE_CEILING
  #define CONTACT_CACHE_CEILING 96      // ... or more than this
#endif
#ifndef CONTACT_STORE_CEILING
  #define CONTACT_STORE_CEILING 1000    // most records the store will hold, however much room there is
#endif
#ifndef CONTACT_HEAP_MARGIN
  #define CONTACT_HEAP_MARGIN 40000     // heap left alone for the radio stack, BLE or Wi-Fi, which start after the contacts
#endif
#ifndef CONTACT_FS_RESERVE
  #define CONTACT_FS_RESERVE 8192       // file-system bytes left alone for prefs, identity, channels, blobs and the atomic swaps
#endif
#ifndef CONTACT_HEAP_PERCENT
  #define CONTACT_HEAP_PERCENT 40       // share of the heap above the margin that the contact memory may use
#endif

#if defined(SIM_FREE_HEAP)
  uint32_t sim_free_heap();                                  // the simulator's configurable heap budget
  inline uint32_t contactFreeHeap() { return sim_free_heap(); }
#elif defined(ESP32)
  #include <Arduino.h>
  inline uint32_t contactFreeHeap() { return ESP.getFreeHeap(); }
#elif defined(RP2040_PLATFORM)
  #include <Arduino.h>
  inline uint32_t contactFreeHeap() { return rp2040.getFreeHeap(); }
#elif defined(NRF52_PLATFORM)
  #include <Arduino.h>
  inline uint32_t contactFreeHeap() { int f = dbgHeapFree(); return f > 0 ? (uint32_t)f : 0; }
#else
  inline uint32_t contactFreeHeap() { return 0; }            // unknown: the floors apply
#endif

// The configuration for a Companion: `nominal` is the board's MAX_CONTACTS, the number the stock build held.
inline ContactStoreConfig contactStoreConfigFor(int nominal) {
  ContactStoreConfig c;
  c.free_heap = contactFreeHeap();
  c.heap_margin = CONTACT_HEAP_MARGIN;
  c.heap_percent = CONTACT_HEAP_PERCENT;
  c.fs_reserve = CONTACT_FS_RESERVE;
  c.cache_floor = CONTACT_CACHE_FLOOR;
  c.cache_ceiling = CONTACT_CACHE_CEILING;
  c.store_floor = (uint16_t)nominal;
  c.store_ceiling = (uint16_t)(nominal > CONTACT_STORE_CEILING ? nominal : CONTACT_STORE_CEILING);
  return c;
}
