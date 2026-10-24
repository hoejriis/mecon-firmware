#pragma once
// The device interface on the Companion (docs/contract/DEVICE_INTERFACE.md): CMD_MECON (0x7F) frames over USB and
// BLE are reassembled per transport, dispatched through the interface core (src/helpers/iface/) with the caller's
// real context, and the results are queued per transport and handed out one frame per loop pass.
#include <stddef.h>
#include <stdint.h>
#include <helpers/iface/IfaceDispatch.h>
#include <helpers/iface/IfaceConfig.h>
#include <helpers/iface/IfaceFrames.h>

class MyMesh;
class BaseSerialInterface;

#define CMD_MECON 0x7F   // the one Companion code the interface uses, both directions

class MeconIface {
public:
  void begin(MyMesh* mesh, const uint8_t device_id[16], uint32_t epoch);
  // One CMD_MECON frame from the transport the current command arrived on (called while the reply is bound to it).
  void onFrame(const BaseSerialInterface* src, const uint8_t* frame, size_t len, uint32_t now);
  // Hands out pending frames, at most one per transport per call, and drops stale partial messages.
  void loop(uint32_t now);

  MyMesh* mesh() const { return _mesh; }
  Iface::ConfigHost configHost() { return Iface::ConfigHost{cliRun, cliAllow, this}; }
  const char* deviceIdHex() const { return _id_hex; }
  uint32_t epoch() const { return _epoch; }

  static const size_t QUEUE_BYTES = 2048;   // per transport, allocated on its first message
  static const size_t RESULT_MAX = 1536;    // largest result an operation writes in one piece (pages above that)

private:
  struct Link {
    const BaseSerialInterface* src = nullptr;
    Iface::Reassembler rx;
    Iface::OutQueue* q = nullptr;
  };
  static const int LINKS = 4;               // USB, BLE, TCP and one spare

  // Every operation is decided by the CLI privilege table with the caller's real context (design section 6): an
  // interface request never gets more than the same caller's CLI line would.
  static bool authorize(void* mesh, const Iface::OpEntry& op, const Iface::Request& req);
  static bool cliRun(void* self, const char* line, char* reply, size_t cap);
  static bool cliAllow(void* self, const char* line);
  Link* linkFor(const BaseSerialInterface* src, bool create);
  bool queueResult(Link& l, const char* msg, size_t len);

  Link _links[LINKS];
  Iface::ResultCache* _cache = nullptr;     // one for the device (design section 3), allocated on the first request
  MyMesh* _mesh = nullptr;
  char _id_hex[33] = {0};
  uint32_t _epoch = 0;
};
