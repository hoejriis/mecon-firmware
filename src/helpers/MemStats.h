#pragma once
// Run-time memory figures for `get mecon.mem`: what the build cannot show (static RAM is in the CI memory table).
// free      bytes of heap free now
// largest   the largest single block that can be allocated now (fragmentation)
// min_free  the lowest free heap since boot (high-water mark), where the platform tracks it; 0 = not tracked
// psram     bytes of PSRAM free now, on a board that has PSRAM; 0 = none. The three figures above are internal RAM
//           only: the BLE, Wi-Fi and TLS stacks allocate from it, and a PSRAM block would hide its exhaustion.
// loop_stack_free  the least free stack the calling task (the Arduino loop, where every CLI command runs) has had
//           since it started, in bytes; 0 = not tracked
#include <stdint.h>

struct MemStats {
  uint32_t free = 0, largest = 0, min_free = 0, psram = 0, loop_stack_free = 0;
  bool known = false;   // false on a platform without a heap API: the numbers are not measurements
};

#if defined(SIM_FREE_HEAP)
  uint32_t sim_free_heap();                    // the boot budget (--heap-budget)
  void sim_heap_now(uint32_t& free, uint32_t& largest);   // what the test has set since (--heap-file), else the budget
  inline MemStats memStats() { MemStats m; sim_heap_now(m.free, m.largest); m.known = true; return m; }
#elif defined(ESP32)
  #include <esp_heap_caps.h>
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  inline MemStats memStats() {
    MemStats m;
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    m.free = heap_caps_get_free_size(internal);
    m.largest = heap_caps_get_largest_free_block(internal);
    m.min_free = heap_caps_get_minimum_free_size(internal);
    m.psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    m.loop_stack_free = uxTaskGetStackHighWaterMark(NULL);   // bytes on ESP-IDF (StackType_t is a byte)
    m.known = true;
    return m;
  }
#elif defined(RP2040_PLATFORM)
  #include <Arduino.h>
  inline MemStats memStats() { MemStats m; m.free = m.largest = rp2040.getFreeHeap(); m.known = true; return m; }
#elif defined(NRF52_PLATFORM)
  #include <Arduino.h>
  inline MemStats memStats() { MemStats m; int f = dbgHeapFree(); m.free = m.largest = f > 0 ? (uint32_t)f : 0; m.known = true; return m; }
#else
  inline MemStats memStats() { return MemStats(); }
#endif
