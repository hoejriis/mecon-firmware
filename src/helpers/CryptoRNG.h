#pragma once
// Random bytes from the platform's cryptographic RNG, fed by its hardware RNG, for values that must not be predictable:
// device_id, the BLE PIN, the local unlock challenge and the channel nonces (docs/contract/AUTHORITY.md section 12).
//
// Not StdRNG (ArduinoHelpers.h), which is Arduino random(): on some cores a libc PRNG seeded once with 32 bits, so
// everything drawn from it follows from one small seed. The node identity is not drawn from either: it comes from the
// radio's noise (RadioNoiseListener).
//
// Platforms without a cryptographic RNG here fall back to the RNG the caller passes, with a build warning, so a port
// knows it has work to do.
#include <stddef.h>
#include <stdint.h>
#include <Mesh.h>

#if defined(ESP_PLATFORM)
  #include <esp_random.h>
#elif defined(NRF52_PLATFORM)
  #include <nrf_soc.h>
#elif defined(ARDUINO_ARCH_RP2040)
  #include <Arduino.h>
#elif defined(__linux__) || defined(__APPLE__)
  #include <stdio.h>
#endif

class CryptoRNG : public mesh::RNG {
public:
  explicit CryptoRNG(mesh::RNG* fallback = nullptr) : _fallback(fallback) { }

  void random(uint8_t* dest, size_t sz) override {
#if defined(ESP_PLATFORM)
    esp_fill_random(dest, sz);   // the hardware RNG, whitened; full entropy while the radio (Wi-Fi or BLE) is on
#elif defined(NRF52_PLATFORM)
    // The SoftDevice owns the RNG peripheral and keeps a pool of hardware random bytes; wait for enough to be there.
    size_t done = 0;
    while (done < sz) {
      uint8_t avail = 0;
      sd_rand_application_bytes_available_get(&avail);
      if (avail == 0) continue;
      uint8_t n = (uint8_t)((sz - done) < avail ? (sz - done) : avail);
      if (sd_rand_application_vector_get(dest + done, n) == 0) done += n;
    }
#elif defined(ARDUINO_ARCH_RP2040)
    for (size_t i = 0; i < sz; i += 4) {
      uint32_t r = rp2040.hwrand32();
      for (size_t k = 0; k < 4 && i + k < sz; k++) dest[i + k] = (uint8_t)(r >> (8 * k));
    }
#elif defined(__linux__) || defined(__APPLE__)
    // The simulated boards and the host tests are not a security target, and their screen captures and fixtures must
    // repeat: the seeded fallback when the caller gives one, /dev/urandom otherwise.
    if (_fallback) {
      _fallback->random(dest, sz);
      return;
    }
    FILE* f = fopen("/dev/urandom", "rb");
    size_t got = f ? fread(dest, 1, sz, f) : 0;
    if (f) fclose(f);
    (void)got;
#else
  #warning "CryptoRNG: no cryptographic RNG for this platform; falling back to the caller's RNG"
    if (_fallback) _fallback->random(dest, sz);
#endif
  }

private:
  mesh::RNG* _fallback;
};
