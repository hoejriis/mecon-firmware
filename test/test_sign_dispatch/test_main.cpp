#include <gtest/gtest.h>
#include <string.h>
#include "helpers/iface/SignDispatch.h"

// The dispatch of a signature to the nRF52 worker (Issue #641): src/helpers/iface/SignDispatch.h. NrfWorker::run itself
// is nRF52-only (its non-nRF52 stand-in runs a job inline); these tests give the dispatch a stand-in with the same shape.

namespace {

struct FakeKey {
  mutable int calls = 0;
  mutable bool on_worker_seen = false;
  void sign(uint8_t* sig, const uint8_t* msg, int len) const;
};

bool g_on_worker = false;   // set by a stand-in while it "is" the worker task

void FakeKey::sign(uint8_t* sig, const uint8_t* msg, int len) const {
  calls++;
  on_worker_seen = g_on_worker;
  for (int i = 0; i < 64; i++) sig[i] = (uint8_t)(i < len ? msg[i] ^ 0x5A : 0xA5);
}

int g_runs = 0;
uint32_t g_wait = 0;

bool onWorker(void (*job)(void*), void* ctx, uint32_t wait_ms) {   // hands the job over and waits, as NrfWorker::run does
  g_runs++;
  g_wait = wait_ms;
  g_on_worker = true;
  job(ctx);
  g_on_worker = false;
  return true;
}

bool inlineOnWorker(void (*job)(void*), void* ctx, uint32_t) {   // NrfWorker::run called on the worker: the job runs inline
  g_runs++;
  job(ctx);
  return true;
}

bool cannotRun(void (*)(void*), void*, uint32_t) {   // the worker could not be created: nothing ran
  g_runs++;
  return false;
}

const uint8_t MSG[] = {1, 2, 3, 4, 5};

void expectSig(const uint8_t* sig) {
  for (int i = 0; i < 64; i++) EXPECT_EQ(sig[i], (uint8_t)(i < 5 ? MSG[i] ^ 0x5A : 0xA5)) << i;
}

}  // namespace

class SignDispatch : public ::testing::Test {
protected:
  void SetUp() override { g_runs = 0; g_wait = 0; g_on_worker = false; }
};

TEST_F(SignDispatch, TheSignatureIsComputedOnTheWorkerAndOnlyOnce) {
  FakeKey k;
  uint8_t sig[64] = {0};
  Iface::signVia(onWorker, &k, sig, MSG, sizeof(MSG));
  EXPECT_EQ(g_runs, 1);
  EXPECT_EQ(k.calls, 1);
  EXPECT_TRUE(k.on_worker_seen);
  EXPECT_EQ(g_wait, Iface::SIGN_WAIT_MS);
  expectSig(sig);
}

TEST_F(SignDispatch, OnTheWorkerAlreadyItRunsInlineOnceNeverHandedToItself) {
  FakeKey k;
  uint8_t sig[64] = {0};
  g_on_worker = true;   // the caller is a request on the worker
  Iface::signVia(inlineOnWorker, &k, sig, MSG, sizeof(MSG));
  EXPECT_EQ(g_runs, 1);
  EXPECT_EQ(k.calls, 1);
  expectSig(sig);
}

TEST_F(SignDispatch, WithNoWorkerItIsSignedOnTheLoopAsBefore) {
  FakeKey k;
  uint8_t sig[64] = {0};
  Iface::signVia(cannotRun, &k, sig, MSG, sizeof(MSG));
  EXPECT_EQ(g_runs, 1);
  EXPECT_EQ(k.calls, 1);
  EXPECT_FALSE(k.on_worker_seen);
  expectSig(sig);
}

TEST_F(SignDispatch, ABusyWorkerIsNotSignedTwice) {
  FakeKey k;
  uint8_t sig[64] = {0};
  Iface::signVia(onWorker, &k, sig, MSG, sizeof(MSG), 1234);
  EXPECT_EQ(k.calls, 1);   // a job that ran is never repeated on the loop
  EXPECT_EQ(g_wait, 1234u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
