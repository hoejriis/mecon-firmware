// Link stub for the idf5_hybrid_libs env (platformio.ini): the firmware itself is never used.
// Weak, because the hybrid library pass links its own placeholder sketch with strong setup()/loop() as well.
#include <Arduino.h>
__attribute__((weak)) void setup() {}
__attribute__((weak)) void loop() {}
