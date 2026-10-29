#include "NrfWorker.h"

#if defined(NRF52_PLATFORM)
#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

namespace NrfWorker {

namespace {

SemaphoreHandle_t g_req = nullptr, g_done = nullptr;
TaskHandle_t g_task = nullptr, g_loop = nullptr;
volatile Job g_job = nullptr;
void* volatile g_ctx = nullptr;
volatile bool g_retired = false;

void body(void*) {
  for (;;) {
    if (xSemaphoreTake(g_req, portMAX_DELAY) != pdTRUE) continue;
    const Job j = g_job;
    if (j) j(g_ctx);
    g_job = nullptr;
    xSemaphoreGive(g_done);
  }
}

bool start() {
  if (g_task) return true;
  g_req = xSemaphoreCreateBinary();
  g_done = xSemaphoreCreateBinary();
  if (!g_req || !g_done) return false;
  // configSTACK_DEPTH_TYPE is in words on this port.
  return xTaskCreate(body, "iface", STACK_BYTES / sizeof(StackType_t), nullptr, TASK_PRIO_LOW, &g_task) == pdPASS;
}

}  // namespace

__attribute__((noinline)) bool run(Job job, void* ctx, uint32_t wait_ms) {
  if (g_retired) return false;
  const TaskHandle_t me = xTaskGetCurrentTaskHandle();
  if (g_task && me == g_task) {   // already on the worker: run inline
    job(ctx);
    return true;
  }
  if (!g_loop) g_loop = me;
  if (!start()) return false;
  g_ctx = ctx;
  g_job = job;
  xSemaphoreGive(g_req);
  if (xSemaphoreTake(g_done, pdMS_TO_TICKS(wait_ms)) == pdTRUE) return true;
  // The job overran and is still running, on the caller's buffers and on state the loop would now touch too. Nothing
  // safe can follow: not a reply, not freeing those buffers. The device resets (a fault, as a watchdog would).
  g_retired = true;
  NVIC_SystemReset();
  return false;
}

uint32_t loopStackFree() { return g_loop ? (uint32_t)uxTaskGetStackHighWaterMark(g_loop) * sizeof(StackType_t) : 0; }
uint32_t workerStackFree() { return g_task ? (uint32_t)uxTaskGetStackHighWaterMark(g_task) * sizeof(StackType_t) : 0; }
bool retired() { return g_retired; }

}  // namespace NrfWorker

#else

namespace NrfWorker {
bool run(Job job, void* ctx, uint32_t) {
  job(ctx);
  return true;
}
uint32_t loopStackFree() { return 0; }
uint32_t workerStackFree() { return 0; }
bool retired() { return false; }
}  // namespace NrfWorker

#endif
