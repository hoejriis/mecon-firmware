#pragma once
// The update slot and the boot selection (docs/contract/UPDATES.md sections 6 and 7): the platform part of an install.
//
//   ESP32 (ESP_PLATFORM)   esp_ota: the inactive OTA partition, written sequentially (each sector is erased as the write
//                          reaches it), activated with esp_ota_set_boot_partition. The bootloader's app rollback is the
//                          first stage of the rollback: an image boots pending-verify and is marked valid once its
//                          start-up checks pass (markValid); a reset before that returns to the previous slot.
//   simulated boards       files beside the data directory: <dir>.ota0 and <dir>.ota1 hold the two slots' images and
//                          <dir>.otadata the running slot, the slot the next boot runs and the pending-verify flag. The
//                          start of a node is its bootloader: slotBoot() switches slots and rolls back as ESP32 does.
//   anything else          no slot: every call fails.
#include <stddef.h>
#include <stdint.h>

namespace Upd {

// At boot, before anything reads the running image: on the simulated board, the bootloader's slot switch and rollback.
void slotBoot();

uint32_t slotCapacity();   // bytes the inactive slot holds; 0 when there is none

// A sequential write of the inactive slot. open() starts one (abandoning any other); write() appends at `offset`, which
// must be the number of bytes written so far; read() reads back what was written.
bool slotOpen(uint32_t size);
bool slotWrite(uint32_t offset, const uint8_t* p, size_t n);
bool slotRead(uint32_t offset, uint8_t* p, size_t n);
// The write is complete: the platform checks the image's own format (esp_ota_end). False: it is not a bootable image.
bool slotFinish();
// Abandons a write and erases the first sector, so the slot holds no bootable image; eraseStep() erases the rest one
// sector per call and returns false once nothing is left. Erasing never runs in one go: it would hold the loop.
void slotAbandon();
bool slotEraseStep();

bool slotActivate();   // the next boot runs the written image, on trial (pending-verify)

// The slots by identity: the partition's flash address on ESP32, 1 or 2 on the simulated board, 0 where there is none.
// What runs and what boots next is read from the slots themselves, never inferred from an image's hash.
uint32_t slotRunning();            // the slot the running image came from
uint32_t slotBooting();            // the slot the next boot runs
uint32_t slotWritten();            // the slot an install writes: the one not running
bool slotSetBoot(uint32_t id);     // the next boot runs that slot's image, on trial: ESP-IDF writes every new boot
                                   // entry as NEW, so even an image that ran before must prove itself again

// SHA-256 of the running application image: exactly its bytes, as a manifest's image.sha256 names them. Computed once
// at boot (about 1.7 MB read); false where the platform cannot say.
bool runningHash(uint8_t out[32]);

// The running image's version, as preflight orders it and attestation reports it: the compiled version
// (mecon::versionFull()). The simulated board's image may declare another in "<dir>.version" (one line), read once a
// boot, because a build container without git tags compiles "unknown", which orders below every min_from.
const char* runningVersion();

// A target whose bootloader installs the image itself (UPDATES.md section 6, kind bootloader-dfu): the nRF52 with the
// Adafruit bootloader, or a simulated board started as one ("<dir>.dfu-board" exists). slotEnterDfu() resets into the
// bootloader's serial DFU (GPREGRET 0x4e); the simulated board exits with status 76 for the test to play the bootloader.
bool slotIsDfuBoard();
void slotEnterDfu();

enum RunState : uint8_t { RS_VALID, RS_PENDING, RS_UNKNOWN };
RunState runningState();   // pending: booted from an install and not yet marked valid (the bootloader's rollback window)
bool markValid();          // ends the rollback window

}  // namespace Upd
