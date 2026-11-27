#pragma once
// Where an Ed25519 signature is computed on an nRF52 (Issue #641). The Adafruit core runs loop() on a fixed 4 KB stack. A
// signature costs about 1.6 KB of it (Provenance::sign 288 B, ed25519_sign 520 B, sha512_compress 800 B) and it is reached
// from the deepest loop paths (an inventory tick, a console line, a frame handler), so the worst static path on the loop
// was within 512 B of the gate's limit and a 12-hour soak left 844 B free.
//
// DESIGN. The narrowest place that covers every signed event, result and report is the one call that computes the
// signature, in Provenance::sign and Provenance::makeReport. On an nRF52 it runs through NrfWorker::run, which hands the
// job to the 8 KB interface worker and blocks the loop task until it is done (src/helpers/NrfWorker.h).
//  - Ordering and bytes: the loop waits, so nothing else runs meanwhile and the message, the sequence number and the
//    frame order are exactly as before; Ed25519 is deterministic, so the signature is byte-identical.
//  - Re-entrancy: a signature reached from a request already running on the worker (the result of an interface request)
//    meets NrfWorker::run on the worker itself, which runs the job inline (it never waits on itself).
//  - Buffers: the job reads the message and the key and writes the 64-byte signature, all owned by the caller's frame
//    and not touched by anyone else while the loop waits. Nothing else is read or written; the sequence number is
//    advanced before the call, on the loop, as before.
//  - Failure: run() returns false when the worker could not be created or is retired; the signature is then computed on
//    the loop task, as before this change (signOnLoop). A job that overruns its wait cannot be taken back: it is still
//    running on the caller's buffers, so NrfWorker::run resets the device instead of returning (same rule as every other
//    job). A signature takes milliseconds, so the wait is generous.
//  - Other platforms: NrfWorker::run runs the job inline there, and Provenance calls sign() directly, so an ESP32 build
//    is unchanged.
// signJob must stay noinline (folded into its caller, its frames would count against the loop task), and signOnLoop is a
// separate function so the stack gate can tell the degraded mode apart (tools/stack_callgraph.py, --no-reentry).
#include <stddef.h>
#include <stdint.h>

namespace Iface {

static const uint32_t SIGN_WAIT_MS = 15000;

template <class Key>
struct SignCall {
  const Key* key;
  uint8_t* sig;
  const uint8_t* msg;
  int len;
};

template <class Key>
__attribute__((noinline)) void signJob(void* c) {
  const SignCall<Key>* s = static_cast<const SignCall<Key>*>(c);
  s->key->sign(s->sig, s->msg, s->len);
}

// The worker could not run it: on the loop task, as before.
template <class Key>
__attribute__((noinline)) void signOnLoop(const SignCall<Key>& c) {
  signJob<Key>(const_cast<SignCall<Key>*>(&c));
}

// run: NrfWorker::run, or a test's stand-in with the same shape (job, ctx, wait_ms) -> bool.
template <class Key, class Run>
inline void signVia(Run run, const Key* key, uint8_t* sig, const uint8_t* msg, int len, uint32_t wait_ms = SIGN_WAIT_MS) {
  SignCall<Key> c = {key, sig, msg, len};
  if (!run(&signJob<Key>, &c, wait_ms)) signOnLoop<Key>(c);
}

}  // namespace Iface
