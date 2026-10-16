// Force-included into project sources only (see platformio.ini).
// The Crypto library exports a hardware-AES class when ESP32 is defined; the host
// has no such peripheral. Include its header once with ESP32 hidden so the portable
// software AES128 is the one the sources see (its include guard keeps it that way).
#pragma once
#pragma push_macro("ESP32")
#undef ESP32
#include <AES.h>
#pragma pop_macro("ESP32")
