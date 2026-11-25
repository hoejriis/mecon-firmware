#pragma once
// Which parts of the update code a build carries.
//
// UPD_LORA: a firmware update over LoRa, the target's side (docs/contract/LORA_UPDATES.md section 8): update.manifest.part,
// update.offer's source.lora, the paused job state's transfer, the bulk codec (LoraBulk, LoraTransfer) and the slot's resumed
// write. It is carried where the contract enables the path: an ESP32 build, which has two application slots, and the simulator.
// Any other build (the nRF52 envs) carries none of it, and answers a LoRa offer `unsupported-source`. The native unit tests
// define it in platformio.ini. -D UPD_NO_LORA leaves it out of a build that would otherwise have it (the tests use that to
// prove the answer), and a build may define UPD_LORA itself.
#if !defined(UPD_LORA) && !defined(UPD_NO_LORA) && (defined(ESP_PLATFORM) || defined(SIM_FREE_HEAP))
#define UPD_LORA 1
#endif
