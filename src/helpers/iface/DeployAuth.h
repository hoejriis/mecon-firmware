#pragma once
// The Deployment authority layer of the device interface (docs/contract/AUTHORITY.md), shared by the Companion
// (examples/companion_radio/MeconIface) and the Repeater and Room server console (ConsoleIface). It owns the loaded
// record and its sequence marks, checks and replay-protects signed requests before they run, issues the channel
// nonces, and implements deployment.get / provision / authority_update / reset and the unenrolment paths. It touches
// no file system itself (Dep::CopyStore), knows nothing about a transport, and never prints or returns a secret.
// Fail closed throughout: anything that cannot be decided is a refusal.
//
// The host calls setContext() with the caller of the request it is about to dispatch, asks preAuthorize() from its
// authorization callback, and routes a request that carries a `mecon_auth` wrapper through dispatchSigned(), which
// runs the inner request back through the host (RunFn) as the signed principal.
#include <stddef.h>
#include <stdint.h>
#include "IfaceCache.h"
#include "IfaceDispatch.h"
#include "IfaceJson.h"
#include "helpers/cli/CliPolicy.h"
#include "helpers/deployment/DepState.h"
#include "helpers/deployment/DepStore.h"
#include "helpers/deployment/DepWrapper.h"

namespace Iface {

class DeployAuth {
public:
  struct Env {
    Dep::CopyStore* store = nullptr;
    const uint8_t* node_key = nullptr;   // this device's node public key, 32 bytes (the device binding of a request)
    bool (*profile_exists)(void* user, uint8_t index) = nullptr;   // an MQTT profile index exists; null: none do
    void* user = nullptr;
    // The Repeater and Room server's command handlers learn from Cli::serverCaller() whether the device is enrolled and
    // whether the console line they run is a verified signed request; the Companion has its own path for both.
    bool publish_caller = false;
  };

  // Loads the record and its marks from the store; called once at boot. With no record a standalone device holds no
  // DepState at all.
  void begin(const Env& env);

  // Whether the device holds a Deployment record, healthy or corrupt: an unsigned caller then changes nothing.
  bool enrolled() const { return _dep_ls != Dep::LS_ABSENT; }
  Dep::LoadState state() const { return _dep_ls; }
  bool isSigned() const { return _signed; }
  // The caller of the request about to be dispatched (a transport, or the broker).
  void setContext(const Cli::Context& c) { _ctx = c; }

  // The channel nonce hello reports on a link: empty (nullptr) when the device holds no record.
  const char* helloNonce(const char* link_nonce) const;
  static void issueNonce(char slot[33]);
  // status.deployment: nothing for a device with no record.
  void writeStatus(JsonWriter& w, bool hardened);

  // Decided before the privilege table: the open operations are allowed, an unsigned state change on an enrolled device
  // is refused with a contract code in *deny, and everything else is the table's.
  enum Pre { PRE_ALLOW, PRE_DENY, PRE_TABLE };
  Pre preAuthorize(const OpEntry& op, const char** deny);

  // ---- signed requests
  static bool isWrapper(const char* msg, size_t len);
  // Runs the inner request (body, body_len) as the signed principal ctx and writes its result envelope to out.
  typedef int (*RunFn)(void* user, const Cli::Context& ctx, const char* body, size_t body_len, char* out, size_t cap);
  // uctx: the transport's caller. nonce_slot: this link's channel nonce (replaced after a sensitive operation). cache:
  // the device's result cache, for the stored result of a duplicate (may be null).
  int dispatchSigned(const Cli::Context& uctx, char nonce_slot[33], const char* msg, size_t len, ResultCache* cache,
                     RunFn run, void* run_user, char* out, size_t cap);

  // ---- the operations (OpContext.user is the host; the host forwards to these)
  Status opGet(OpContext& c);
  Status opProvision(OpContext& c);
  Status opUpdate(OpContext& c);
  Status opReset(OpContext& c);

  // After the file system has been formatted (the factory reset): the provisioned marker goes with the record, and the
  // state in memory follows what is now stored.
  void onFactoryReset();
  // The Companion's rescue console rebuild must not unenrol (sections 9, 11): the record's two copies, the marks' two
  // copies and the marker are read before it and written back after. A null snapshot means something exists that cannot
  // be read; the rebuild is then refused.
  struct RecordSnapshot;
  RecordSnapshot* snapshotRecord();
  bool restoreRecord(RecordSnapshot* s);
  static void freeSnapshot(RecordSnapshot* s);

private:
  struct DepWork;
  void load();
  void loadInner();
  void syncCaller();
  bool persistMarks();
  bool readCurrent(DepWork* w);
  bool commit(DepWork* w);
  bool saveMarksFor(const Dep::AuthoritySet& set);

  Env _env;
  Cli::Context _ctx;                        // the caller of the request being handled
  Dep::DepState* _dep = nullptr;            // the loaded record; null with no record (a standalone device) or a corrupt one
  Dep::LoadState _dep_ls = Dep::LS_ABSENT;  // LS_ABSENT: no Deployment; LS_CORRUPT: something exists, nothing usable
  bool _signed = false;                     // the request being dispatched passed AUTHORITY.md section 7
  uint32_t _last_update_ms = 0;             // rate limit of deployment.authority_update (flash wear)
  bool _have_update = false;
};

}  // namespace Iface
