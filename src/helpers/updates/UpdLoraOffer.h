#pragma once
// The LoRa source of an update.offer (docs/contract/LORA_UPDATES.md section 4), read and checked before preflight:
// source: {"lora": {"block", "window_end", "preset"?}}, and the offer's own "issued_at", the manager's clock when it signed
// the offer, which the window is measured from. Header-only: the native tests drive it.
#include <stdint.h>
#include "helpers/iface/IfaceJson.h"
#include "helpers/iface/LoraTransfer.h"

namespace Upd {

struct LoraSource {
  unsigned block;
  uint32_t window_end;
  uint32_t issued_at;
};

enum LoraSourceVerdict : uint8_t {
  LS_OK,
  LS_BAD_ARGS,      // invalid, bad-args: malformed, a block outside 1..8, a window that is missing, past or over 12 hours
  LS_UNSUPPORTED    // refused, unsupported: a preset (the fast preset is a later slice)
};

// `src` is the token of the offer's `source` object (it holds "lora"); `args` the offer's args. device_unix is the
// device's own wall clock, valid when unix_known.
inline LoraSourceVerdict readLoraSource(const Iface::JsonDoc& d, int args, int lora, bool unix_known, uint32_t device_unix,
                                        LoraSource* out) {
  using namespace Iface;
  int64_t block, window_end, issued_at;
  if (!isObject(d, lora)) return LS_BAD_ARGS;
  const int tb = jsonFind(d, lora, "block"), tw = jsonFind(d, lora, "window_end"), tp = jsonFind(d, lora, "preset");
  // Nothing but the three keys the contract names: any other is malformed.
  if (d.tok[lora].size != (tb >= 0) + (tw >= 0) + (tp >= 0)) return LS_BAD_ARGS;
  if (!jsonGetInt(d, tb, 1, BULK_BLOCK_MAX, &block)) return LS_BAD_ARGS;
  if (!jsonGetInt(d, tw, 1, 0xFFFFFFFFLL, &window_end)) return LS_BAD_ARGS;
  if (!jsonGetInt(d, jsonFind(d, args, "issued_at"), 1, 0xFFFFFFFFLL, &issued_at)) return LS_BAD_ARGS;
  if (!windowValid((uint32_t)window_end, (uint32_t)issued_at)) return LS_BAD_ARGS;
  // A window the device's own clock says is over is past (a wrong clock can only shorten a window, never lengthen it).
  if (unix_known && (int32_t)((uint32_t)window_end - device_unix) <= 0) return LS_BAD_ARGS;
  // The fast preset (LORA_UPDATES.md section 9) is a later slice: any preset is refused here.
  if (tp >= 0) return LS_UNSUPPORTED;
  out->block = (unsigned)block;
  out->window_end = (uint32_t)window_end;
  out->issued_at = (uint32_t)issued_at;
  return LS_OK;
}

}  // namespace Upd
