#pragma once
// The update operations (docs/contract/UPDATES.md sections 5 to 8): update.manifest, update.status, update.offer and
// update.chunk, and the install they drive. One instance per device interface; the Companion and the Repeater and Room
// server console share the code.
//
// update.manifest keeps up to two manifests in RAM, keyed by their SHA-256, for an update.offer to name. A stage
// verifies the release signature and the target at once (preflight steps 5 to 7) and answers what the rest of
// preflight would say. The slots are heap, freed on eviction; a reboot empties them.
//
// update.offer runs preflight and opens the update job. This build delivers by push only (source {"push": true}): the
// offer's result carries the job's transfer token, and update.chunk writes the bytes, in order, into the inactive slot.
// The last chunk starts the read-back: the slot's SHA-256, one piece per loop pass, against the manifest. A match
// activates the image (the job awaits confirmation across the reboot); the image that boots attests, and job.confirm or
// the deadline decides. The role passes the job service and its hooks (Host) and calls loop(), and its job hooks hand
// an update job to jobRevert() and jobConfirm().
#include <stddef.h>
#include <stdint.h>
#include "UpdPreflight.h"
#include "UpdUrl.h"

// A download over IP (source.url) needs a network: the builds with the MQTT controller's, on ESP32 and the simulated board.
#if defined(ENABLE_MQTT) && (defined(ESP_PLATFORM) || defined(SIM_FREE_HEAP))
#define UPD_DOWNLOAD 1
#endif
#include "helpers/deployment/DepHmac.h"
#include "helpers/iface/IfaceDispatch.h"
#include "helpers/iface/JobService.h"

#ifndef MECON_LAYOUT
#define MECON_LAYOUT 1           // the partition or flash-layout version this build is made for
#endif
#ifndef MECON_DATA_FORMAT
#define MECON_DATA_FORMAT 1      // the version of the on-flash data this build reads and writes (section 9)
#endif
#ifndef MECON_HARDWARE_REV
#define MECON_HARDWARE_REV "1"   // this board's revision, as a manifest's hardware_rev lists it
#endif
#ifndef MECON_VARIANT
#define MECON_VARIANT "unknown"  // the build environment; bin/mecon_version.py passes it
#endif

namespace Upd {

struct Record;

class UpdateService {
public:
  static const uint32_t STAGE_GAP_MS = 1000;              // at most one stage a second: each costs a signature check
  static const uint32_t OFFER_GAP_MS = 60 * 1000;         // an offer that would erase the slot again (preflight 2)
  static const uint32_t STALL_MS = 10 * 60 * 1000;        // no chunk for this long fails the job (section 6)
  static const uint32_t VALID_AFTER_MS = 20 * 1000;       // the start-up checks: the loop has run this long (stage 1)
  static const uint8_t MAX_SEND_BACK = 3;                   // boots that send an unconfirmed image back to the previous one
  static const uint32_t CONFIRM_MIN_S = 30, CONFIRM_MAX_S = 3600, CONFIRM_DEFAULT_S = 900;
  static const size_t CHUNK_MAX = 2304;                    // decoded bytes per update.chunk
  static const uint32_t DOWNLOAD_MS = 30 * 60 * 1000;      // a download not finished this long after accepted fails
  static const uint32_t HEAD_TIMEOUT_MS = 15 * 1000;       // the connect, and the response head
  static const uint32_t DOWNLOAD_PIECE = 4096;             // body bytes taken per loop pass
  // A download's own connection, measured on a Heltec V3 (2026-10-31): plain HTTP took about 23 KB, HTTPS about 63 KB and
  // left 3.5 KB at its lowest. So HTTPS asks for the measured cost and a 32 KB margin, and a board without PSRAM is
  // refused it (busy, low-memory) and uses HTTP, which the digest makes just as safe.
  static const uint32_t HEAP_NEEDED_HTTP = 48 * 1024;
  static const uint32_t HEAP_NEEDED_HTTPS = 96 * 1024;
  static const uint32_t READBACK_PIECE = 4096;             // bytes hashed per loop pass
  static const uint32_t HEAP_NEEDED = 48 * 1024;
  static constexpr const char* OP = "update.offer";

  struct Slot {
    char* text;            // the manifest's bytes, heap; nullptr when free
    uint16_t len;
    uint8_t sha256[32];
    uint8_t sig[SIG_LEN];
    uint32_t staged_ms;
  };

  // What the role lends the service: its job service, and how it names a request's job, records the caller, says
  // whether the request is Deployment-signed, and reboots (after the answer has gone out).
  struct Host {
    Iface::JobService* jobs;
    void* user;
    bool (*job_id)(void* user, const Iface::Request& req, char* out, size_t cap);
    void (*record_caller)(void* user, Iface::Job& j);
    bool (*signed_now)(void* user);
    void (*reboot)(void* user);
  };

  // hardware and role as hello reports them; both outlive the service. Runs the simulated bootloader's slot switch and
  // hashes the running image, so call it before the job table is loaded (its revert hook needs both).
  // `hardware_names`: every name the board can report, '|'-separated (MainBoard::getManufacturerNames), or null.
  void begin(const char* hardware, const char* role, const char* hardware_names = nullptr);
  void setHost(const Host& h) { _host = h; }

  // After the job table is loaded: an update job that activated learns whether its image runs (attestation) or the
  // bootloader rolled it back.
  void bootReconcile();

  // { "manifest": "<text>", "manifest_sig": "<base64>" } -> { "sha256", "version", "preflight" }
  Iface::Status opManifest(Iface::OpContext& c, uint32_t now_ms);
  // { "job", "running": { "version", "sha256" }, "security_floor", "staged": [{ "sha256", "version" }] }
  Iface::Status opStatus(Iface::OpContext& c);
  // { "manifest_sha256", "source": {"push": true}, "confirm": {"within_s"} } -> { "job", "state", "transfer_token", "next" }
  Iface::Status opOffer(Iface::OpContext& c, uint32_t now_ms);
  // { "job", "token", "offset", "data" } -> { "next" }
  Iface::Status opChunk(Iface::OpContext& c, uint32_t now_ms);

  // One piece of work: the stall deadline, a piece of the read-back, a sector of erase, the end of the rollback window.
  void loop(uint32_t now_ms);

  // The role's job hooks, for a job whose op is OP. revert: puts the previous image back (or abandons the transfer) and
  // sets the terminal state; confirm: only once the new image runs.
  static bool owns(const Iface::Job& j);
  bool jobRevert(Iface::Job& j);
  bool jobConfirm(Iface::Job& j);

  // "update": {"job", "version", "sha256"} while the image of an update job runs unconfirmed (attestation, section 7
  // step 3), into hello and the status snapshot, with "unconfirmed": true once its job is over without a confirmation
  // and the previous image kept failing its trial (step 6); nothing otherwise.
  void writeAttestation(Iface::JsonWriter& w) const;

  // The staged manifest with this hash, or nullptr (manifest-not-staged).
  const Slot* staged(const uint8_t sha256[32]) const;

  // The device's facts for preflight, read now (heap, slot, floor and running version included).
  Device device() const;

private:
  enum Phase : uint8_t { IDLE, RECEIVING, DOWNLOADING, VERIFYING };
  // Putting the previous image back: it already runs (and the next boot runs it too), the next boot runs it, which slot
  // holds it cannot be told, or the slots could not be changed.
  enum Back : uint8_t { BACK_RUNS, BACK_REBOOT, BACK_UNKNOWN, BACK_FAILED };

  Slot _s[2] = {};
  uint32_t _last_stage_ms = 0;
  bool _staged_once = false;
  uint32_t _last_offer_ms = 0;
  bool _offered_once = false;
  const char* _hardware = "";
  const char* _hardware_names = nullptr;
  const char* _role = "";
  Host _host = {};

  // The running image, hashed at boot.
  uint8_t _running_sha[32] = {};
  bool _running_known = false;
  bool _attested = false;     // an update job's image runs and awaits confirmation
  bool _stranded = false;     // an image never confirmed runs, and the previous one kept failing its trial (section 7)
  char _stranded_job[33] = {};
  char _attested_job[33] = {};
  bool _valid_marked = false;

  // The transfer of the open update job (RAM: a reboot before activation reverts the job).
  Phase _phase = IDLE;
  char _job[33] = {};
  uint8_t _token[16] = {};
  uint8_t _image_sha[32] = {};
  char _version[25] = {};
  uint32_t _size = 0, _received = 0, _readback_at = 0, _last_chunk_ms = 0, _within_s = 0;
  Dep::Sha256Ctx* _readback = nullptr;
  bool _erasing = false;

#ifdef UPD_DOWNLOAD
  // The download (source.url). The connect and the response head run on the network's worker, which writes _dl_err
  // (nullptr: the body follows) and _dl_status; the main loop reads them once the worker is idle. Only in the builds
  // that can download: the URL alone is about 270 bytes of RAM the others would carry for nothing.
  Url _url = {};
  uint32_t _accepted_ms = 0;
  bool _dl_started = false, _dl_connected = false, _dl_close_pending = false;
  const char* _dl_err = nullptr;
  int _dl_status = 0;
  static void dlJob(void* self);
  const char* dlConnect();
  void downloadStep(uint32_t now_ms);
#endif

  Iface::JobTable* table() const { return _host.jobs ? _host.jobs->table() : nullptr; }
  void persist() { if (_host.jobs) _host.jobs->persist(); }
  Iface::Job* openUpdateJob() const;
  void endTransfer(bool erase);
  void failJob(const char* error, const char* extra = nullptr);
  void activate();
  int writeRunning(char* out, size_t cap, bool previous, const char* prev_version, const uint8_t* prev_sha) const;
  size_t resultObject(char* out, size_t cap, const char* members, bool previous, const char* prev_version,
                      const uint8_t* prev_sha) const;
  void markProven();
  Back backToPrevious(const Record* r);
  bool revertJob(Iface::Job& j, const Record* r, const char* reason);
  void confirmFromRecord(Iface::Job& j);
  void settleRecord();
  void reconcile();
  void finish(Iface::Job& j, uint8_t state, const char* members);
};

}  // namespace Upd
