// The job operations (src/helpers/iface/JobService.h) through the real dispatcher: job.get, job.list, job.ack,
// job.cancel and job.confirm, the status member, persistence through a JobStore, and the boot and deadline rules.
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "helpers/iface/IfaceDispatch.h"
#include "helpers/iface/JobService.h"

using namespace Iface;

struct RamStore : JobStore {
  std::vector<uint8_t> file;
  int saves = 0;
  bool fail = false;
  size_t load(uint8_t* buf, size_t cap) override {
    if (file.empty() || file.size() > cap) return 0;
    memcpy(buf, file.data(), file.size());
    return file.size();
  }
  bool save(const uint8_t* buf, size_t len) override {
    if (fail) return false;
    file.assign(buf, buf + len);
    saves++;
    return true;
  }
};

struct Hooks {
  int reverts = 0, confirms = 0;
  bool revert_ok = true, confirm_ok = true;
  JobService* svc = nullptr;
};

static bool revertHook(void* u, Job& j) {
  Hooks* h = static_cast<Hooks*>(u);
  h->reverts++;
  if (!h->revert_ok) return false;
  h->svc->table()->setState(&j, JOB_REVERTED);
  return true;
}
static bool confirmHook(void* u, Job&) {
  Hooks* h = static_cast<Hooks*>(u);
  h->confirms++;
  return h->confirm_ok;
}

static JobService* S(OpContext& c) { return static_cast<JobService*>(c.user); }
static Status oGet(OpContext& c) { return S(c)->opGet(c); }
static Status oList(OpContext& c) { return S(c)->opList(c); }
static Status oAck(OpContext& c) { return S(c)->opAck(c); }
static Status oCancel(OpContext& c) { return S(c)->opCancel(c); }
static Status oConfirm(OpContext& c) { return S(c)->opConfirm(c); }
static bool allow(void*, const OpEntry&, const Request&) { return true; }
static const OpEntry OPS[] = {
  {"job.get", oGet, PRIV_READ, false}, {"job.list", oList, PRIV_READ, false}, {"job.ack", oAck, PRIV_READ, false},
  {"job.cancel", oCancel, PRIV_WRITE, false}, {"job.confirm", oConfirm, PRIV_WRITE, false},
};

static std::string call(JobService& s, const std::string& op, const std::string& args = "") {
  Dispatcher d(OPS, sizeof(OPS) / sizeof(OPS[0]), allow, nullptr, &s);
  const std::string m = R"({"v":1,"id":"q","op":")" + op + "\"" + (args.empty() ? "" : R"(,"args":)" + args) + "}";
  std::vector<char> out(800);
  const int n = d.handle(m.data(), m.size(), out.data(), out.size(), nullptr);
  return n < 0 ? "<overflow>" : std::string(out.data(), n);
}

static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

TEST(JobService, AnEmptyTableListsNothingAndDoesNotAllocate) {
  JobService s;
  EXPECT_EQ(R"({"v":1,"id":"q","status":"ok","result":{"jobs":[],"open":0,"owed":0}})", call(s, "job.list"));
  EXPECT_TRUE(has(call(s, "job.get", R"({"job":"x"})"), R"("status":"invalid","error":"unknown-job")"));
  char b[64];
  JsonWriter w(b, sizeof(b));
  w.beginObject(); s.writeStatus(w); w.endObject();
  EXPECT_STREQ(R"({"jobs":{"open":0,"owed":0}})", b);
}

TEST(JobService, GetListAndAckFollowTheTable) {
  JobService s;
  RamStore st;
  s.begin(&st, false, nullptr, nullptr, nullptr);
  Job* j = s.table()->open("r-1", "config.apply");
  ASSERT_NE(nullptr, j);
  const std::string r1 = call(s, "job.get", R"({"job":"r-1"})");
  EXPECT_TRUE(has(r1, R"("job":"r-1","op":"config.apply","state":"accepted","owed":false,"result":null)")) << r1;
  EXPECT_TRUE(has(call(s, "job.ack", R"({"job":"r-1"})"), R"("error":"not-terminal")")) << "running work is not a result";
  ASSERT_TRUE(s.table()->setState(j, JOB_APPLIED, R"({"applied":{"name":"x"}})", 24));
  EXPECT_TRUE(has(call(s, "job.list"), R"("jobs":[{"job":"r-1","op":"config.apply","state":"applied","owed":true}],"open":0,"owed":1)"));
  EXPECT_TRUE(has(call(s, "job.get", R"({"job":"r-1"})"), R"("result":{"applied":{"name":"x"}})"));
  EXPECT_TRUE(has(call(s, "job.ack", R"({"job":"r-1"})"), R"("acked":true)"));
  EXPECT_TRUE(has(call(s, "job.list"), R"("owed":0)"));
  EXPECT_GE(st.saves, 1) << "an acknowledgement is saved";
  EXPECT_TRUE(has(call(s, "job.get", "{}"), R"("error":"bad-args")"));
}

TEST(JobService, ASavedTableComesBackAfterARestart) {
  RamStore st;
  {
    JobService a;
    a.begin(&st, false, nullptr, nullptr, nullptr);
    Job* j = a.table()->open("keep", "config.apply");
    a.table()->setState(j, JOB_FAILED, R"({"error":"x"})", 13);
    ASSERT_TRUE(a.persist());
  }
  JobService b;
  b.begin(&st, false, nullptr, nullptr, nullptr);
  EXPECT_TRUE(has(call(b, "job.get", R"({"job":"keep"})"), R"("state":"failed","owed":true)"));
}

TEST(JobService, ADamagedSavedTableIsForgottenAndReplaced) {
  RamStore st;
  {
    JobService a;
    a.begin(&st, false, nullptr, nullptr, nullptr);
    a.table()->open("gone", "config.apply");
    ASSERT_TRUE(a.persist());
  }
  st.file[st.file.size() / 2] ^= 0xFF;
  JobService b;
  b.begin(&st, false, nullptr, nullptr, nullptr);
  EXPECT_TRUE(has(call(b, "job.get", R"({"job":"gone"})"), R"("error":"unknown-job")"));
  JobTable check;
  EXPECT_TRUE(check.deserialize(st.file.data(), st.file.size())) << "the store now holds an intact, empty table";
}

TEST(JobService, ASecondBootRevertsAPendingChangeAtLoad) {
  RamStore st;
  Hooks h;
  {
    JobService a;
    a.begin(&st, false, nullptr, nullptr, nullptr);
    Job* j = a.table()->open("pend", "config.apply");
    ASSERT_TRUE(a.table()->awaitConfirm(j, 300, true));
    ASSERT_TRUE(a.persist());
  }
  JobService b;
  h.svc = &b;
  b.begin(&st, false, revertHook, confirmHook, &h);
  EXPECT_EQ(1, h.reverts);
  EXPECT_TRUE(has(call(b, "job.get", R"({"job":"pend"})"), R"("state":"reverted")"));
}

TEST(JobService, ARevertTheRoleCannotDoEndsFailed) {
  RamStore st;
  Hooks h;
  h.revert_ok = false;
  {
    JobService a;
    a.begin(&st, false, nullptr, nullptr, nullptr);
    Job* j = a.table()->open("stuck", "config.apply");
    ASSERT_TRUE(a.table()->awaitConfirm(j, 300, true));
    ASSERT_TRUE(a.persist());
  }
  JobService b;
  h.svc = &b;
  b.begin(&st, false, revertHook, confirmHook, &h);
  const std::string r = call(b, "job.get", R"({"job":"stuck"})");
  EXPECT_TRUE(has(r, R"("state":"failed")") && has(r, "revert-failed")) << "never left pending forever: " << r;
}

TEST(JobService, ConfirmAndCancelNeedTheRightStateAndTheRolesHook) {
  JobService s;
  Hooks h;
  h.svc = &s;
  RamStore st;
  s.begin(&st, false, revertHook, confirmHook, &h);
  Job* j = s.table()->open("c1", "config.apply");
  EXPECT_TRUE(has(call(s, "job.confirm", R"({"job":"c1"})"), R"("error":"not-awaiting")"));
  ASSERT_TRUE(s.table()->awaitConfirm(j, 300, true));
  h.confirm_ok = false;
  EXPECT_TRUE(has(call(s, "job.confirm", R"({"job":"c1"})"), R"("error":"confirm-failed")"));
  h.confirm_ok = true;
  EXPECT_TRUE(has(call(s, "job.confirm", R"({"job":"c1"})"), R"("state":"confirmed")"));
  EXPECT_TRUE(has(call(s, "job.cancel", R"({"job":"c1"})"), R"("error":"not-cancellable")"));
  Job* k = s.table()->open("c2", "config.apply");
  ASSERT_NE(nullptr, k);
  EXPECT_TRUE(has(call(s, "job.cancel", R"({"job":"c2"})"), R"("state":"cancelled")")) << "nothing applied yet";
  Job* m = s.table()->open("c3", "config.apply");
  ASSERT_TRUE(s.table()->awaitConfirm(m, 300, true));
  EXPECT_TRUE(has(call(s, "job.cancel", R"({"job":"c3"})"), R"("state":"reverted")")) << "after applying, cancel is the revert";
  EXPECT_EQ(1, h.reverts);
}

TEST(JobService, WithoutHooksAPendingChangeCannotBeConfirmed) {
  JobService s;
  Job* j = s.table()->open("n", "config.apply");
  ASSERT_TRUE(s.table()->awaitConfirm(j, 300, true));
  EXPECT_TRUE(has(call(s, "job.confirm", R"({"job":"n"})"), R"("error":"confirm-failed")"));
}

TEST(JobService, TheDeadlineRunsOnLoopTime) {
  JobService s;
  Hooks h;
  h.svc = &s;
  RamStore st;
  s.begin(&st, false, revertHook, confirmHook, &h);
  Job* j = s.table()->open("t", "config.apply");
  ASSERT_TRUE(s.table()->awaitConfirm(j, 3, true));
  s.tick(1000);   // the first call only starts the clock
  s.tick(2999);
  EXPECT_EQ(0, h.reverts);
  s.tick(4000);
  EXPECT_EQ(1, h.reverts);
  EXPECT_EQ(JOB_REVERTED, j->state);
}

TEST(JobService, AFailedSaveAnswersBusy) {
  JobService s;
  RamStore st;
  s.begin(&st, false, nullptr, nullptr, nullptr);
  Job* j = s.table()->open("b", "config.apply");
  s.table()->setState(j, JOB_APPLIED);
  st.fail = true;
  EXPECT_TRUE(has(call(s, "job.ack", R"({"job":"b"})"), R"("status":"busy","error":"storage")"));
}

TEST(JobService, TheBootCountIsSavedSoARebootLoopStillReverts) {
  RamStore st;
  Hooks h;
  {
    JobService a;
    a.begin(&st, false, nullptr, nullptr, nullptr);
    Job* j = a.table()->open("loop", "config.apply");
    ASSERT_TRUE(a.table()->awaitConfirm(j, 300, false));   // takes effect after its reboot: that one boot is expected
    ASSERT_TRUE(a.persist());
  }
  for (int boot = 1; boot <= 2; boot++) {   // two boots, each well inside the deadline, with no tick in between
    JobService b;
    h.svc = &b;
    b.begin(&st, false, revertHook, confirmHook, &h);
    EXPECT_EQ(boot == 2 ? 1 : 0, h.reverts) << "boot " << boot;
  }
}

TEST(JobService, TheTimeLeftIsSavedSoARebootDoesNotStartItOver) {
  RamStore st;
  {
    JobService a;
    a.begin(&st, false, nullptr, nullptr, nullptr);
    Job* j = a.table()->open("slow", "config.apply");
    ASSERT_TRUE(a.table()->awaitConfirm(j, 100, true));
    ASSERT_TRUE(a.persist());
    a.tick(0);
    a.tick(29000);
    const int saves = st.saves;
    a.tick(31000);
    EXPECT_EQ(saves + 1, st.saves) << "saved once SAVE_EVERY_S had passed";
  }
  JobTable t;
  ASSERT_TRUE(t.deserialize(st.file.data(), st.file.size()));
  Job* j = nullptr;
  ASSERT_EQ(JobTable::FOUND, t.find("slow", &j));
  EXPECT_EQ(69u, j->confirm_left_s);
}

static bool noPosture(void*, uint8_t cls) { return !(cls & (1u << 3)); }   // this caller lacks class 3

TEST(JobService, ConfirmAndCancelNeedEveryClassTheJobChanged) {
  JobService s;
  Hooks h;
  h.svc = &s;
  RamStore st;
  s.begin(&st, false, revertHook, confirmHook, &h);
  s.setClassCheck(noPosture, nullptr);
  Job* j = s.table()->open("p", "config.apply");
  j->cls = (1u << 1) | (1u << 3);
  ASSERT_TRUE(s.table()->awaitConfirm(j, 300, true));
  EXPECT_TRUE(has(call(s, "job.confirm", R"({"job":"p"})"), R"("status":"refused","error":"not-permitted")"));
  EXPECT_TRUE(has(call(s, "job.cancel", R"({"job":"p"})"), R"("status":"refused","error":"not-permitted")"));
  EXPECT_EQ(0, h.confirms + h.reverts) << "neither hook ran";
  j->cls = 1u << 1;
  EXPECT_TRUE(has(call(s, "job.confirm", R"({"job":"p"})"), R"("state":"confirmed")"));
}

TEST(JobService, ASignedJobIsNamedByItsIdentity) {
  JobService s;
  ASSERT_NE(nullptr, s.table()->open("#a1b2:7", "config.apply"));
  EXPECT_TRUE(has(call(s, "job.get", R"({"job":"#a1b2:7"})"), R"("job":"#a1b2:7")"));
  EXPECT_TRUE(has(call(s, "job.get", R"({"job":"a b"})"), R"("error":"bad-args")")) << "still a narrow charset";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
