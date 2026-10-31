#pragma once
// Random bytes from the platform's cryptographic RNG, fed by its hardware RNG, for values that must not be predictable:
// device_id, the BLE PIN, the local unlock challenge and the channel nonces (docs/contract/AUTHORITY.md section 12).
//
// Not StdRNG (ArduinoHelpers.h), which is Arduino random(): on some cores a libc PRNG seeded once with 32 bits, so
// everything drawn from it follows from one small seed. The node identity's seed (RadioNoiseListener) is this RNG, or
// the CC310's TRNG, mixed with the radio's noise.
//
// Platforms without a cryptographic RNG here fall back to the RNG the caller passes, with a build warning, so a port
// knows it has work to do.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <Mesh.h>

#if defined(ESP_PLATFORM)
  #include <esp_random.h>
#elif defined(NRF52_PLATFORM)
  #include <nrf_soc.h>
  #include <nrf_sdm.h>   // sd_softdevice_is_enabled: included here, not left to whatever a user included first
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
    if (!nrf52Fill(dest, sz)) {
      // Neither source delivered in its bound: the caller's RNG, never a hang. Without one the output is zero, which the
      // callers that need secrecy (the nonce, the challenge) refuse.
      if (_fallback) _fallback->random(dest, sz);
      else memset(dest, 0, sz);
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
#if defined(NRF52_PLATFORM)
  // Polls are bounded by count: this runs before the SoftDevice may be up (the Companion draws its BLE PIN at boot), so
  // it can neither wait on the SoftDevice's pool nor assume it exists.
  static const uint32_t RNG_POLLS = 400000;

  static bool nrf52Fill(uint8_t* dest, size_t sz) {
    uint8_t sd_enabled = 0;
    if (sd_softdevice_is_enabled(&sd_enabled) != NRF_SUCCESS) return false;
    if (sd_enabled) {
      // The SoftDevice owns the RNG peripheral and keeps a pool of hardware random bytes.
      size_t done = 0;
      uint32_t polls = 0;
      while (done < sz) {
        uint8_t avail = 0;
        if (sd_rand_application_bytes_available_get(&avail) != NRF_SUCCESS) return false;
        if (avail == 0) {
          if (++polls > RNG_POLLS) return false;
          continue;
        }
        uint8_t n = (uint8_t)((sz - done) < avail ? (sz - done) : avail);
        if (sd_rand_application_vector_get(dest + done, n) != NRF_SUCCESS) return false;
        done += n;
      }
      return true;
    }
    // No SoftDevice: the peripheral is ours. Bias correction on (it costs time, not entropy), one byte per VALRDY.
    NRF_RNG->CONFIG = RNG_CONFIG_DERCEN_Enabled << RNG_CONFIG_DERCEN_Pos;
    NRF_RNG->EVENTS_VALRDY = 0;
    NRF_RNG->TASKS_START = 1;
    bool ok = true;
    for (size_t i = 0; i < sz && ok; i++) {
      uint32_t polls = 0;
      while (NRF_RNG->EVENTS_VALRDY == 0) {
        if (++polls > RNG_POLLS) { ok = false; break; }
      }
      if (!ok) break;
      dest[i] = (uint8_t)NRF_RNG->VALUE;
      NRF_RNG->EVENTS_VALRDY = 0;
    }
    NRF_RNG->TASKS_STOP = 1;
    return ok;
  }
#endif
};
