#pragma once
// Signed events and results (docs/contract/AUTHORITY.md section 3.4), shared by the Companion and the Repeater/Room
// console. While the device holds a Deployment record, every interface event and result is signed with the node key,
// so its origin survives a Reader bridge or a broker (both forward the bytes unchanged).
//
//   {...,"usig_boot":<boot epoch>,"usig_seq":<n>,"usig":"<base64 Ed25519>"}
//
// The signature is over "MECON-UPLINK-v1" 0x00 <stream, or "result"> 0x00 <the object text up to and including the
// closing brace, usig_seq the last member>. usig is appended as the last member. When this boot's epoch could not be saved
// (it might repeat), nothing is signed and the object carries "unsigned":"epoch_unsaved" instead. Fail closed: a message
// that cannot be signed is never sent as if it had been.
#include <stddef.h>
#include <stdint.h>
#include <Identity.h>
#include "DeployAuth.h"

namespace Iface {

// Standard base64 with padding; out holds 4*ceil(n/3)+1 bytes.
void base64Encode(char* out, const uint8_t* in, size_t n);

class Provenance {
public:
  // id: the node identity (read at signing time, so a rotation takes effect at once). auth: whether a record exists.
  void begin(const mesh::LocalIdentity* id, const DeployAuth* auth, uint32_t boot_epoch, bool epoch_saved) {
    _id = id; _auth = auth; _boot = boot_epoch; _epoch_saved = epoch_saved; _seq = 0;
  }

  // buf holds one JSON object of len bytes, ending in '}'; cap is its capacity. Returns the new length, or -1 when the
  // object cannot be signed within cap (the caller drops an event, and answers a result with a short busy). A device with
  // no record returns len unchanged. stream: an event's stream name, or "result". signer: the key to sign with, or null for
  // the node key as it is now (identity.rotate signs its own result with the key being retired).
  int sign(const char* stream, char* buf, size_t len, size_t cap, const mesh::LocalIdentity* signer = nullptr);
  // A result: if it cannot be signed in place, it is replaced by a short signed busy. -1 only when even that does not fit.
  int signResult(char* buf, size_t len, size_t cap, const mesh::LocalIdentity* signer = nullptr);

  uint32_t seq() const { return _seq; }

private:
  const mesh::LocalIdentity* _id = nullptr;
  const DeployAuth* _auth = nullptr;
  uint32_t _boot = 0;
  bool _epoch_saved = false;
  uint32_t _seq = 0;
};

}  // namespace Iface
