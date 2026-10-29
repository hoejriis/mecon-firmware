#pragma once
// A worker task for the nRF52's heavy device-interface work (#158). The Adafruit core runs loop() on a fixed 4 KB
// stack (LOOP_STACK_SZ, 256 words of 4 bytes, times 4), and a first deployment.provision overflowed it: parse, the
// authority-set verify (ge_double_scalarmult_vartime alone is 2,352 bytes) and the signing of the result. Here the loop
// hands one job to a task with an 8 KB stack and blocks until it is done, so the job sees the same single-threaded
// world (prefs, the file system and the radio queue are not touched meanwhile) on a stack that has room.
//
// The rules:
// - A job that overruns its wait resets the device. It is still running, on the caller's buffers and on state the loop
//   would touch next, so no reply and no free of those buffers is safe; a reset is what a watchdog would do.
// - One caller: run() is called only from the Arduino loop task (and from the worker itself, inline).
// - A run() from the worker itself runs the job inline (waiting on itself would time out).
// - The task is created on the first run(); if it cannot be, run() returns false.
// - Jobs must be noinline: folded into the caller, their frames would count against the loop task in the stack gate
//   (tools/stack_callgraph.py) and in fact.
// Other platforms run the job inline: their loop stacks are 8 KB or more.
#include <stddef.h>
#include <stdint.h>

namespace NrfWorker {

typedef void (*Job)(void* ctx);

static const uint32_t STACK_BYTES = 8192;

// Runs job(ctx) on the worker and returns once it has finished; false when the worker could not be created (the caller
// then refuses the request). A job that overruns wait_ms resets the device and does not return.
bool run(Job job, void* ctx, uint32_t wait_ms);

// The least free stack, in bytes, since boot: of the Arduino loop task (recorded on the first run()), and of the worker.
// 0 when not known (not yet run, or not an nRF52 build).
uint32_t loopStackFree();
uint32_t workerStackFree();
bool retired();

}  // namespace NrfWorker
