#pragma once
// A boot-time seed that keeps later random draws strong when the hardware source alone is not. On an ESP32 the hardware
// RNG has full entropy only while Wi-Fi or BLE runs, and a Repeater or Room server may run neither. Its other noise source
// (the SAR ADC) must not run beside the radio or another ADC user, so it cannot serve a draw at any moment. It seeds this
// pool once in setup() instead (CryptoRNG::seedAtBoot), and each draw then mixes in a SHA-256 stream from the pool.
//
// The seed moves on after every draw, so a RAM read cannot reconstruct earlier output.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <Utils.h>

class EntropyPool {
public:
  static const size_t POOL_SIZE = 32;

  void seed(const uint8_t* bytes) {
    memcpy(_seed, bytes, POOL_SIZE);
    _seeded = true;
  }
  bool seeded() const { return _seeded; }

  // XORs the stream into dest, so the output is no weaker than what dest already holds. Before a seed it leaves dest as it
  // is: the hardware source alone, as without this pool.
  void mix(uint8_t* dest, size_t sz) {
    if (!_seeded) return;
    uint8_t in[POOL_SIZE + sizeof(uint32_t) + 1];
    uint8_t block[POOL_SIZE];
    memcpy(in, _seed, POOL_SIZE);
    in[POOL_SIZE + sizeof(uint32_t)] = 0;   // 0: output blocks; 1: the next seed
    uint32_t counter = 0;
    for (size_t off = 0; off < sz; off += POOL_SIZE, counter++) {
      memcpy(in + POOL_SIZE, &counter, sizeof(counter));
      mesh::Utils::sha256(block, sizeof(block), in, sizeof(in));
      const size_t n = sz - off < POOL_SIZE ? sz - off : POOL_SIZE;
      for (size_t k = 0; k < n; k++) dest[off + k] ^= block[k];
    }
    in[POOL_SIZE + sizeof(uint32_t)] = 1;
    mesh::Utils::sha256(_seed, POOL_SIZE, in, sizeof(in));
    wipe(in, sizeof(in));
    wipe(block, sizeof(block));
  }

private:
  static void wipe(uint8_t* p, size_t n) {
    volatile uint8_t* v = p;
    while (n--) *v++ = 0;
  }
  uint8_t _seed[POOL_SIZE];
  bool _seeded = false;
};
