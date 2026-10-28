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
    _tokens = RATE_BURST * 1000; _tok_ms = 0; _tok_init = false;
  }

  // buf holds one JSON object of len bytes, ending in '}'; cap is its capacity. Returns the new length, or -1 when the
  // object cannot be signed within cap (the caller drops an event, and answers a result with a short busy). A device with
  // no record returns len unchanged. stream: an event's stream name, or "result". signer: the key to sign with, or null for
  // the node key as it is now (identity.rotate signs its own result with the key being retired).
  // extra: members added before the signature fields, already in the form ,"k":v (a result names what it answers).
  int sign(const char* stream, char* buf, size_t len, size_t cap, const mesh::LocalIdentity* signer = nullptr,
           const char* extra = "");
  // A result: it names what it answers (answering: "op", and "job_id" when the request had one) and is signed. If it cannot
  // be signed in place, it is replaced by a short signed busy. -1 only when even that does not fit. A refusal is signed
  // within a budget of RATE_PER_S signatures a second (burst RATE_BURST): beyond it the refusal goes out unsigned with
  // "unsigned":"rate", so unauthenticated input cannot keep the signer busy. Every other result is always signed.
  // was_enrolled: the device held a record when the request began, so the result of the operation that removes the record
  // (deployment.reset) is still signed, and the manager can check that the device did unenrol.
  int signResult(char* buf, size_t len, size_t cap, const mesh::LocalIdentity* signer = nullptr,
                 const Answering* answering = nullptr, bool was_enrolled = false);
  static const uint32_t RATE_PER_S = 8, RATE_BURST = 8;

  uint32_t seq() const { return _seq; }

private:
  const mesh::LocalIdentity* _id = nullptr;
  const DeployAuth* _auth = nullptr;
  uint32_t _boot = 0;
  bool _epoch_saved = false;
  uint32_t _seq = 0;
  uint32_t _tokens = 0;      // refusal-signing budget in thousandths of a signature
  uint32_t _tok_ms = 0;
  bool _tok_init = false;
  bool _force = false;       // sign this result although the record is gone now (see signResult)
  bool allowRefusalSignature();
  int signResultInner(char* buf, size_t len, size_t cap, const mesh::LocalIdentity* signer, const Answering* answering);
};

}  // namespace Iface
