// The management channel binding (src/helpers/iface/MgmtChannel.cpp, docs/contract/MANAGEMENT_CHANNEL.md) over a fake
// native channel table, in-memory record and snapshot files and the real job service: bind, rotate, unbind, the stale
// state, the public-channel refusal, and confirm-or-revert for a caller other than the cable.
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <string.h>
#include <helpers/iface/MgmtChannel.h>

using namespace Iface;

namespace {

struct FakeTable {
  static const int N = 8;
  std::string name[N];
  uint8_t key[N][CHANNEL_SECRET_BYTES] = {{0}};
  bool fail = false;
};

bool getCb(void* u, int slot, ChannelInfo& out) {
  FakeTable* t = static_cast<FakeTable*>(u);
  if (slot < 0 || slot >= FakeTable::N) return false;
  snprintf(out.name, sizeof(out.name), "%s", t->name[slot].c_str());
  out.hash = t->key[slot][0];
  out.key_bytes = CHANNEL_SECRET_BYTES;
  out.keyed = false;
  for (unsigned i = 0; i < CHANNEL_SECRET_BYTES; i++) out.keyed |= t->key[slot][i] != 0;
  return true;
}
bool setCb(void* u, int slot, const char* name, const uint8_t* key) {
  FakeTable* t = static_cast<FakeTable*>(u);
  if (t->fail || slot < 0 || slot >= FakeTable::N) return false;
  t->name[slot] = name;
  memcpy(t->key[slot], key, CHANNEL_SECRET_BYTES);
  return true;
}
int findCb(void* u, const uint8_t* key) {
  FakeTable* t = static_cast<FakeTable*>(u);
  for (int i = 0; i < FakeTable::N; i++) if (memcmp(t->key[i], key, CHANNEL_SECRET_BYTES) == 0) return i;
  return -1;
}

struct RamFile : SnapStore {
  std::vector<uint8_t> file;
  bool fail = false;
  int discards = 0;
  size_t load(uint8_t* buf, size_t cap) override {
    if (file.empty() || file.size() > cap) return 0;
    memcpy(buf, file.data(), file.size());
    return file.size();
  }
  bool save(const uint8_t* buf, size_t len) override {
    if (fail) return false;
    file.assign(buf, buf + len);
    return true;
  }
  void discard() override { file.clear(); discards++; }
};

struct RamJobs : JobStore {
  std::vector<uint8_t> file;
  size_t load(uint8_t* buf, size_t cap) override {
    if (file.empty() || file.size() > cap) return 0;
    memcpy(buf, file.data(), file.size());
    return file.size();
  }
  bool save(const uint8_t* buf, size_t len) override { file.assign(buf, buf + len); return true; }
};

const uint8_t PUBLIC_KEY[CHANNEL_SECRET_BYTES] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                                  0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};

struct Rig;
Rig* g_rig = nullptr;

struct Rig {
  FakeTable t;
  RamFile record, snap;
  RamJobs jobfile;
  JobService jobs;
  ChannelHost ch{FakeTable::N, getCb, setCb, findCb, &t};
  MgmtHost host;
  bool cable = true;
  uint8_t public_fp[MGMT_FP_BYTES];
  ResultCache cache;
  int n = 0;
  uint32_t now_ms = 0;

  static void fp(const uint8_t* key, uint8_t out[MGMT_FP_BYTES]) {   // stands in for SHA-256: distinct per key
    for (unsigned i = 0; i < MGMT_FP_BYTES; i++) out[i] = (uint8_t)(key[i] ^ (uint8_t)(0x5A + i));
  }
  static bool slotFp(void* u, int slot, uint8_t out[MGMT_FP_BYTES]) {
    Rig* r = static_cast<Rig*>(u);
    if (slot < 0 || slot >= FakeTable::N) return false;
    uint8_t any = 0;
    for (unsigned i = 0; i < CHANNEL_SECRET_BYTES; i++) any |= r->t.key[slot][i];
    if (!any) return false;
    fp(r->t.key[slot], out);
    return true;
  }
  static void keyFp(void*, const uint8_t* key, uint8_t out[MGMT_FP_BYTES]) { fp(key, out); }
  static bool readSlot(void* u, int slot, char name[CHANNEL_NAME_MAX + 1], uint8_t key[CHANNEL_SECRET_BYTES]) {
    Rig* r = static_cast<Rig*>(u);
    if (slot < 0 || slot >= FakeTable::N) return false;
    snprintf(name, CHANNEL_NAME_MAX + 1, "%s", r->t.name[slot].c_str());
    memcpy(key, r->t.key[slot], CHANNEL_SECRET_BYTES);
    return true;
  }
  static bool isCable(void* u) { return static_cast<Rig*>(u)->cable; }
  static void recordCaller(void*, Job&) {}
  static bool jobId(void*, const Request& req, char* out, size_t cap) { return JobService::identity(nullptr, req, out, cap); }
  static bool revertHook(void* u, Job& j) {
    Rig* r = static_cast<Rig*>(u);
    if (!mgmtOwnsJob(j) || !mgmtRevert(r->host, j)) return false;
    r->jobs.table()->setState(&j, JOB_REVERTED);
    return true;
  }
  static bool confirmHook(void* u, Job& j) {
    Rig* r = static_cast<Rig*>(u);
    if (!mgmtOwnsJob(j)) return false;
    mgmtConfirmed(r->host);
    return true;
  }

  Rig() {
    fp(PUBLIC_KEY, public_fp);
    host = MgmtHost{&ch, &record, &snap, &jobs, slotFp, keyFp, public_fp, readSlot, isCable, recordCaller, jobId, 2, this};
    jobs.begin(&jobfile, false, revertHook, confirmHook, this);
  }

  static Status oGet(OpContext& c) { return opMgmtGet(c); }
  static Status oPut(OpContext& c) { return mgmtGuardChannel(c, *static_cast<MgmtHost*>(c.user), opChannelPut, "channel.put"); }
  static Status oRemove(OpContext& c) { return mgmtGuardChannel(c, *static_cast<MgmtHost*>(c.user), opChannelRemove, "channel.remove"); }
  static Status oConfirm(OpContext& c) { return g_rig->jobs.opConfirm(c); }
  static Status oJobGet(OpContext& c) { return g_rig->jobs.opGet(c); }
  static bool allow(void*, const OpEntry&, const Request&) { return true; }

  std::string call(const std::string& op, const std::string& args) {
    static const OpEntry OPS[] = {
      {"mgmt.channel.get", oGet, PRIV_READ, false},       {"mgmt.channel.bind", opMgmtBind, PRIV_WRITE, true},
      {"mgmt.channel.rotate", opMgmtRotate, PRIV_WRITE, true}, {"mgmt.channel.unbind", opMgmtUnbind, PRIV_WRITE, true},
      {"channel.put", oPut, PRIV_WRITE, true},            {"channel.remove", oRemove, PRIV_WRITE, true},
      {"job.confirm", oConfirm, PRIV_WRITE, false},       {"job.get", oJobGet, PRIV_READ, false},
    };
    g_rig = this;
    Dispatcher d(OPS, sizeof(OPS) / sizeof(OPS[0]), allow, nullptr, &host);
    std::string m = "{\"v\":1,\"id\":\"t" + std::to_string(++n) + "\",\"op\":\"" + op + "\",\"args\":" + args + "}";
    std::vector<char> out(2048);
    int len = d.handle(m.data(), m.size(), out.data(), out.size(), &cache);
    return len < 0 ? "<overflow>" : std::string(out.data(), len);
  }
  std::string lastId() const { return "t" + std::to_string(n); }
  void slot(int s, const char* nm, uint8_t first) {
    t.name[s] = nm;
    for (unsigned i = 0; i < CHANNEL_SECRET_BYTES; i++) t.key[s][i] = (uint8_t)(first + i);
  }
  void tick(uint32_t seconds) {
    for (uint32_t i = 0; i < seconds; i++) { now_ms += 1000; jobs.tick(now_ms); }
  }
};

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string hexOf(uint8_t first) {
  static const char H[] = "0123456789abcdef";
  std::string s;
  for (unsigned i = 0; i < CHANNEL_SECRET_BYTES; i++) {
    uint8_t b = (uint8_t)(first + i);
    s += H[b >> 4];
    s += H[b & 15];
  }
  return s;
}

}  // namespace

TEST(MgmtChannel, NothingIsBoundAtFirst) {
  Rig r;
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"result\":{\"state\":\"none\"}"));
}

TEST(MgmtChannel, TheCableBindsAPopulatedSlotAndReadsItBack) {
  Rig r;
  r.slot(3, "ops", 0x10);
  std::string res = r.call("mgmt.channel.bind", "{\"slot\":3}");
  EXPECT_TRUE(has(res, "\"state\":\"bound\"")) << res;
  EXPECT_TRUE(has(res, "\"slot\":3")) << res;
  EXPECT_TRUE(has(res, "\"generation\":1")) << res;
  EXPECT_TRUE(has(res, "\"name\":\"ops\"")) << res;
  EXPECT_FALSE(has(res, "\"job\"")) << "the cable needs no confirmation";
  EXPECT_FALSE(has(res, hexOf(0x10))) << "never the key";
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"state\":\"bound\""));
}

TEST(MgmtChannel, AnEmptySlotAndThePublicChannelCannotBeBound) {
  Rig r;
  EXPECT_TRUE(has(r.call("mgmt.channel.bind", "{\"slot\":2}"), "empty-slot"));
  r.t.name[0] = "Public";
  memcpy(r.t.key[0], PUBLIC_KEY, CHANNEL_SECRET_BYTES);
  EXPECT_TRUE(has(r.call("mgmt.channel.bind", "{\"slot\":0}"), "public-channel"));
  EXPECT_TRUE(r.record.file.empty()) << "nothing was written";
  EXPECT_TRUE(has(r.call("mgmt.channel.bind", "{\"slot\":8}"), "\"invalid\""));
}

TEST(MgmtChannel, ASlotChangedUnderneathIsStaleAndCannotBeRotated) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  r.slot(3, "ops", 0x40);   // a native CMD_SET_CHANNEL with another key
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"state\":\"stale\""));
  std::string res = r.call("mgmt.channel.rotate", "{\"secret\":\"" + hexOf(0x70) + "\"}");
  EXPECT_TRUE(has(res, "\"error\":\"stale\"")) << res;
  EXPECT_EQ(0x40, r.t.key[3][0]) << "a stale binding is never moved to a new key";
}

TEST(MgmtChannel, AnUnreadableRecordIsStaleNotAbsent) {
  Rig r;
  r.record.file = {1, 2, 3, 4, 5};
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"state\":\"stale\""));
}

TEST(MgmtChannel, ARemoteCallerMustAskToConfirm) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.cable = false;
  std::string res = r.call("mgmt.channel.bind", "{\"slot\":3}");
  EXPECT_TRUE(has(res, "\"status\":\"invalid\",\"error\":\"confirm-required\"")) << res;
  EXPECT_TRUE(r.record.file.empty());
}

TEST(MgmtChannel, ARemoteBindAwaitsConfirmationAndAConfirmKeepsIt) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.cable = false;
  std::string res = r.call("mgmt.channel.bind", "{\"slot\":3,\"confirm\":{\"within_s\":60}}");
  EXPECT_TRUE(has(res, "\"job_state\":\"awaiting-confirm\"")) << res;
  EXPECT_TRUE(has(res, "\"confirm_left_s\":60")) << res;
  const std::string job = r.lastId();
  EXPECT_FALSE(r.snap.file.empty()) << "the snapshot is written before the change";
  std::string c = r.call("job.confirm", "{\"job\":\"" + job + "\"}");
  EXPECT_TRUE(has(c, "\"ok\"")) << c;
  EXPECT_TRUE(r.snap.file.empty()) << "a confirmed change drops its snapshot";
  r.tick(120);
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"state\":\"bound\"")) << "it stays after the deadline";
}

TEST(MgmtChannel, ARotationNotConfirmedInTimeRevertsTheKeyAndTheRecord) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  r.cable = false;
  std::string res = r.call("mgmt.channel.rotate", "{\"secret\":\"" + hexOf(0x70) + "\",\"confirm\":{\"within_s\":30}}");
  EXPECT_TRUE(has(res, "\"generation\":2")) << res;
  EXPECT_EQ(0x70, r.t.key[3][0]) << "the new key is live while it awaits confirmation";
  r.tick(31);
  EXPECT_EQ(0x10, r.t.key[3][0]) << "the old key is back";
  EXPECT_EQ("ops", r.t.name[3]);
  std::string g = r.call("mgmt.channel.get", "{}");
  EXPECT_TRUE(has(g, "\"state\":\"bound\"")) << g;
  EXPECT_TRUE(has(g, "\"generation\":1")) << g;
  EXPECT_TRUE(r.snap.file.empty());
}

TEST(MgmtChannel, RotationRefusesThePublicKeyADuplicateAndAnUnboundDevice) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.slot(5, "other", 0x30);
  EXPECT_TRUE(has(r.call("mgmt.channel.rotate", "{\"secret\":\"" + hexOf(0x70) + "\"}"), "not-bound"));
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  EXPECT_TRUE(has(r.call("mgmt.channel.rotate", "{\"secret\":\"" + hexOf(0x30) + "\"}"), "duplicate-secret"));
  EXPECT_TRUE(has(r.call("mgmt.channel.rotate", "{\"secret\":\"8b3387e9c5cdea6ac9e5edbaa115cd72\"}"), "public-channel"));
  EXPECT_EQ(0x10, r.t.key[3][0]);
}

TEST(MgmtChannel, ARemotePutToTheBoundSlotNeedsConfirmationAndRevertsAsChannelPut) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  r.cable = false;
  const std::string put = "{\"slot\":3,\"name\":\"ops\",\"secret\":\"" + hexOf(0x50) + "\"";
  EXPECT_TRUE(has(r.call("channel.put", put + "}"), "confirm-required"));
  EXPECT_EQ(0x10, r.t.key[3][0]);
  std::string res = r.call("channel.put", put + ",\"confirm\":{\"within_s\":30}}");
  EXPECT_TRUE(has(res, "\"job_state\":\"awaiting-confirm\"")) << res;
  std::string jg = r.call("job.get", "{\"job\":\"" + r.lastId() + "\"}");
  EXPECT_TRUE(has(jg, "\"op\":\"channel.put\"")) << "the job keeps the operation's own name: " << jg;
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"state\":\"bound\"")) << "the binding moved with the key";
  r.tick(31);
  EXPECT_EQ(0x10, r.t.key[3][0]);
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"generation\":1"));
}

TEST(MgmtChannel, APutToAnotherSlotIsNotGuarded) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  r.cable = false;
  std::string res = r.call("channel.put", "{\"slot\":4,\"name\":\"x\",\"secret\":\"" + hexOf(0x60) + "\"}");
  EXPECT_TRUE(has(res, "\"status\":\"ok\"")) << res;
  EXPECT_FALSE(has(res, "\"job\"")) << res;
}

TEST(MgmtChannel, TheCableRemovingTheBoundSlotEndsTheBinding) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  std::string res = r.call("channel.remove", "{\"slot\":3}");
  EXPECT_TRUE(has(res, "\"removed\":true")) << res;
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"state\":\"none\""));
}

TEST(MgmtChannel, UnbindKeepsTheSlotUnlessAskedToRemoveIt) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  EXPECT_TRUE(has(r.call("mgmt.channel.unbind", "{}"), "\"removed\":false"));
  EXPECT_EQ("ops", r.t.name[3]) << "the channel stays an ordinary channel";
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  EXPECT_TRUE(has(r.call("mgmt.channel.unbind", "{\"remove\":true}"), "\"removed\":true"));
  EXPECT_EQ("", r.t.name[3]);
  EXPECT_EQ(0, r.t.key[3][0]);
}

TEST(MgmtChannel, ARecordThatCannotBeSavedIsAFailureNotAPass) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.record.fail = true;
  std::string res = r.call("mgmt.channel.bind", "{\"slot\":3}");
  EXPECT_TRUE(has(res, "\"status\":\"failed\",\"error\":\"write-failed\"")) << res;
}

TEST(MgmtChannel, ASnapshotNoAwaitingJobOwnsIsRemovedAtBoot) {
  Rig r;
  r.snap.file = {9, 9, 9};
  mgmtSnapBoot(r.host);
  EXPECT_TRUE(r.snap.file.empty());
  EXPECT_EQ(nullptr, r.jobs.peek()) << "the boot check allocates no job table";
}

TEST(MgmtChannel, TheImagesRefuseAFlippedByte) {
  MgmtRecord rec;
  rec.bound = true;
  rec.slot = 4;
  rec.generation = 7;
  uint8_t buf[MgmtRecord::IMAGE];
  const size_t n = rec.serialize(buf);
  MgmtRecord back;
  ASSERT_TRUE(back.parse(buf, n));
  EXPECT_EQ(4, back.slot);
  EXPECT_EQ(7u, back.generation);
  for (size_t i = 0; i < n; i++) {
    uint8_t b[MgmtRecord::IMAGE];
    memcpy(b, buf, n);
    b[i] ^= 0x01;
    EXPECT_FALSE(back.parse(b, n)) << i;
  }
  MgmtSnap s;
  snprintf(s.job, sizeof(s.job), "job-1");
  s.slot = 3;
  s.slot_change = MgmtSnap::SLOT_KEY;
  s.applied_fp[0] = 0x77;
  snprintf(s.name, sizeof(s.name), "ops");
  s.key[0] = 0x42;
  uint8_t img[MgmtSnap::IMAGE_MAX];
  const size_t m = s.serialize(img, sizeof(img));
  MgmtSnap t;
  ASSERT_TRUE(t.parse(img, m));
  EXPECT_STREQ("job-1", t.job);
  EXPECT_EQ(0x42, t.key[0]);
  EXPECT_EQ(MgmtSnap::SLOT_KEY, t.slot_change);
  EXPECT_EQ(0x77, t.applied_fp[0]);
  img[m / 2] ^= 0x01;
  EXPECT_FALSE(t.parse(img, m));
}

TEST(MgmtChannel, ARevertNeverOverwritesAnAppChangeMadeInTheWindow) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  r.cable = false;
  r.call("mgmt.channel.rotate", "{\"secret\":\"" + hexOf(0x70) + "\",\"confirm\":{\"within_s\":30}}");
  const std::string job = r.lastId();
  r.slot(3, "app", 0x20);   // the app writes the slot natively while the rotation awaits confirmation
  r.tick(31);
  EXPECT_EQ(0x20, r.t.key[3][0]) << "the app's key stays";
  EXPECT_EQ("app", r.t.name[3]);
  std::string g = r.call("job.get", "{\"job\":\"" + job + "\"}");
  EXPECT_TRUE(has(g, "\"state\":\"failed\"")) << g;
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"state\":\"stale\""));
  EXPECT_TRUE(r.snap.file.empty()) << "the snapshot goes even when the revert is refused";
}

TEST(MgmtChannel, ARevertOfAChangeThatNeverLandedWritesNothing) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  r.cable = false;
  r.t.fail = true;   // the slot write fails: nothing was applied
  std::string res = r.call("mgmt.channel.rotate", "{\"secret\":\"" + hexOf(0x70) + "\",\"confirm\":{\"within_s\":30}}");
  EXPECT_TRUE(has(res, "\"job_state\":\"failed\"")) << res;
  EXPECT_TRUE(has(res, "\"reverted\":true")) << "an untouched slot is a successful revert: " << res;
  EXPECT_EQ(0x10, r.t.key[3][0]);
  EXPECT_TRUE(has(r.call("mgmt.channel.get", "{}"), "\"state\":\"bound\""));
}

TEST(MgmtChannel, AConfirmThatProtectsNothingIsRefused) {
  Rig r;
  r.slot(3, "ops", 0x10);
  const std::string conf = ",\"confirm\":{\"within_s\":30}";
  EXPECT_TRUE(has(r.call("mgmt.channel.unbind", "{\"remove\":false" + conf + "}"), "confirm-not-needed"));
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  EXPECT_TRUE(has(r.call("mgmt.channel.bind", "{\"slot\":3" + conf + "}"), "confirm-not-needed")) << "already bound";
  EXPECT_TRUE(has(r.call("channel.put", "{\"slot\":4,\"name\":\"x\",\"secret\":\"" + hexOf(0x60) + "\"" + conf + "}"),
                  "confirm-not-needed")) << "not the bound slot";
  EXPECT_EQ("", r.t.name[4]);
}

TEST(MgmtChannel, AnUnreadableRecordNamesNoSlotAndGuardsNothing) {
  Rig r;
  r.t.name[0] = "Public";
  memcpy(r.t.key[0], PUBLIC_KEY, CHANNEL_SECRET_BYTES);
  r.record.file = {1, 2, 3, 4, 5};
  EXPECT_EQ("{\"v\":1,\"id\":\"t1\",\"status\":\"ok\",\"result\":{\"state\":\"stale\"}}", r.call("mgmt.channel.get", "{}"));
  EXPECT_TRUE(has(r.call("mgmt.channel.unbind", "{\"remove\":true}"), "\"error\":\"stale\""));
  EXPECT_EQ("Public", r.t.name[0]) << "slot 0 is never erased on an unreadable record's account";
  EXPECT_TRUE(has(r.call("mgmt.channel.unbind", "{}"), "\"state\":\"none\"")) << "it can still be ended";
}

TEST(MgmtChannel, UnbindDoesNotRemoveAStaleSlot) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  r.slot(3, "app", 0x40);
  EXPECT_TRUE(has(r.call("mgmt.channel.unbind", "{\"remove\":true}"), "\"error\":\"stale\""));
  EXPECT_EQ("app", r.t.name[3]) << "the app's channel stays";
}

TEST(MgmtChannel, AGuardedPutIsCheckedBeforeAnyJobOpens) {
  Rig r;
  r.slot(3, "ops", 0x10);
  r.call("mgmt.channel.bind", "{\"slot\":3}");
  r.cable = false;
  std::string res = r.call("channel.put", "{\"slot\":3,\"name\":\"ops\",\"secret\":\"8b3387e9c5cdea6ac9e5edbaa115cd72\",\"confirm\":{}}");
  EXPECT_TRUE(has(res, "public-channel")) << res;
  res = r.call("channel.put", "{\"slot\":3,\"name\":\"ops\",\"secret\":\"zz\",\"confirm\":{}}");
  EXPECT_TRUE(has(res, "\"status\":\"invalid\"")) << res;
  EXPECT_TRUE(r.snap.file.empty()) << "no snapshot, so no job";
  EXPECT_TRUE(has(r.call("job.get", "{\"job\":\"" + r.lastId() + "\"}"), "unknown-job"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
