// contact.list / get / put / remove / protect over a fake native contact table (src/helpers/iface/ContactOps.cpp,
// docs/contract/OPERATIONS.md section 5.1).
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>
#include <string.h>
#include <helpers/iface/ContactOps.h>

using namespace Iface;

namespace {

// A table of records in record order; a removed record leaves a hole that a later add reuses, as the native store does.
struct FakeTable {
  static const int CAP = 12;
  bool used[CAP] = {false};
  ContactRec rec[CAP];
  uint8_t flags[CAP] = {0};   // the grant bits a put must never touch
  int writes = 0;
  int count() const { int n = 0; for (int i = 0; i < CAP; i++) n += used[i]; return n; }
  int find(const uint8_t* key) const {
    for (int i = 0; i < CAP; i++) if (used[i] && memcmp(rec[i].key, key, 32) == 0) return i;
    return -1;
  }
  int add(const char* name, uint8_t type, int first) {   // a contact whose key repeats `first`
    int i = 0;
    while (i < CAP && used[i]) i++;
    if (i == CAP) return -1;
    used[i] = true;
    memset(&rec[i], 0, sizeof(rec[i]));
    memset(rec[i].key, first, 32);
    snprintf(rec[i].name, sizeof(rec[i].name), "%s", name);
    rec[i].type = type;
    rec[i].hops = -1;
    return i;
  }
};

FakeTable* tab(void* u) { return static_cast<FakeTable*>(u); }
bool getCb(void* u, const uint8_t* key, ContactRec& out) {
  int i = tab(u)->find(key);
  if (i < 0) return false;
  out = tab(u)->rec[i];
  return true;
}
bool nextCb(void* u, int& cursor, ContactRec& out) {
  for (int i = cursor; i < FakeTable::CAP; i++) {
    if (!tab(u)->used[i]) continue;
    out = tab(u)->rec[i];
    cursor = i + 1;
    return true;
  }
  cursor = FakeTable::CAP;
  return false;
}
ContactWrite putCb(void* u, const ContactRec& in) {
  FakeTable* t = tab(u);
  int i = t->find(in.key);
  if (i < 0) {
    i = t->add("", 1, 0);
    if (i < 0) return CW_FULL;
    memcpy(t->rec[i].key, in.key, 32);
  }
  bool fav = t->rec[i].fav;   // flags are the contact's own
  t->rec[i] = in;
  t->rec[i].fav = fav;
  t->rec[i].hops = -1;
  t->writes++;
  return CW_OK;
}
int removeCb(void* u, const uint8_t* key) {
  int i = tab(u)->find(key);
  if (i < 0) return 0;
  tab(u)->used[i] = false;
  tab(u)->writes++;
  return 1;
}
int protectCb(void* u, const uint8_t* key, bool on) {
  int i = tab(u)->find(key);
  if (i < 0) return -1;
  if (tab(u)->rec[i].fav == on) return 0;
  tab(u)->rec[i].fav = on;
  tab(u)->writes++;
  return 1;
}
bool authAll(void*, const OpEntry&, const Request&) { return true; }

const OpEntry OPS[] = {
  {"contact.list", opContactList, PRIV_READ, false},
  {"contact.get", opContactGet, PRIV_READ, false},
  {"contact.put", opContactPut, PRIV_WRITE, true},
  {"contact.remove", opContactRemove, PRIV_WRITE, true},
  {"contact.protect", opContactProtect, PRIV_WRITE, true},
};

std::string hexKey(int b) {
  char s[65];
  for (int i = 0; i < 32; i++) snprintf(s + 2 * i, 3, "%02x", b);
  return s;
}

struct Rig {
  FakeTable t;
  ContactHost host{FakeTable::CAP, 0, getCb, nextCb, putCb, removeCb, protectCb, &t};
  Dispatcher d{OPS, 5, authAll, nullptr, &host};
  ResultCache cache;
  int n = 0;
  std::string call(const std::string& op, const std::string& args) {
    host.count = t.count();
    std::string m = "{\"v\":1,\"id\":\"t" + std::to_string(++n) + "\",\"op\":\"" + op + "\",\"args\":" + args + "}";
    std::vector<char> out(2048);
    int len = d.handle(m.data(), m.size(), out.data(), out.size(), &cache);
    return len < 0 ? "<overflow>" : std::string(out.data(), len);
  }
  std::string put(int b, const char* name, const char* type, const char* more = "") {
    return call("contact.put", "{\"key\":\"" + hexKey(b) + "\",\"name\":\"" + name + "\",\"type\":\"" + type + "\"" + more + "}");
  }
};

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST(ContactOps, PutAddsAContactAndGetReadsItBack) {
  Rig r;
  std::string res = r.put(0xA1, "Ada", "chat", ",\"lat_e6\":55000000,\"lon_e6\":12000000");
  EXPECT_TRUE(has(res, "\"status\":\"ok\"")) << res;
  EXPECT_TRUE(has(res, "\"created\":true") && has(res, "\"changed\":true")) << res;
  EXPECT_EQ(1, r.t.count());
  std::string got = r.call("contact.get", "{\"key\":\"" + hexKey(0xA1) + "\"}");
  EXPECT_TRUE(has(got, "\"name\":\"Ada\"") && has(got, "\"type\":\"chat\"") && has(got, "\"fav\":false")) << got;
  EXPECT_TRUE(has(got, "\"lat_e6\":55000000") && has(got, "\"lon_e6\":12000000")) << got;
  EXPECT_TRUE(has(got, "\"hops\":-1")) << got;
  EXPECT_FALSE(has(got, "path")) << "no path bytes, no secret";
}

TEST(ContactOps, ARepeatedPutIsTheSameStateAndWritesNothing) {
  Rig r;
  r.put(0xA1, "Ada", "chat");
  int w = r.t.writes;
  std::string again = r.put(0xA1, "Ada", "chat");
  EXPECT_TRUE(has(again, "\"created\":false") && has(again, "\"changed\":false")) << again;
  EXPECT_EQ(w, r.t.writes) << "a retry of a put that took effect must not rewrite the contact";
  EXPECT_EQ(1, r.t.count());
}

TEST(ContactOps, AnUpdateChangesOnlyWhatItNamesAndKeepsTheFlags) {
  Rig r;
  r.put(0xA1, "Ada", "chat", ",\"lat_e6\":1,\"lon_e6\":2");
  r.call("contact.protect", "{\"key\":\"" + hexKey(0xA1) + "\",\"protected\":true}");
  std::string res = r.call("contact.put", "{\"key\":\"" + hexKey(0xA1) + "\",\"name\":\"Ada B\"}");
  EXPECT_TRUE(has(res, "\"created\":false") && has(res, "\"changed\":true")) << res;
  EXPECT_TRUE(has(res, "\"name\":\"Ada B\"") && has(res, "\"type\":\"chat\"") && has(res, "\"lat_e6\":1")) << res;
  EXPECT_TRUE(has(res, "\"fav\":true")) << "a put does not clear the protection: " << res;
}

TEST(ContactOps, ANewContactNeedsANameAndATypeAndNeverType0) {
  Rig r;
  const std::string k = "\"key\":\"" + hexKey(1) + "\"";
  const std::string bad[] = {
    "{" + k + ",\"name\":\"x\"}",                           // no type
    "{" + k + ",\"type\":\"chat\"}",                        // no name
    "{" + k + ",\"name\":\"x\",\"type\":\"none\"}",         // type 0 has no spelling
    "{" + k + ",\"name\":\"x\",\"type\":\"0\"}",
    "{" + k + ",\"name\":\"x\",\"type\":\"gateway\"}",
    "{" + k + ",\"name\":\"\",\"type\":\"chat\"}",
    "{" + k + ",\"name\":\"0123456789012345678901234567890123\",\"type\":\"chat\"}",
    "{" + k + ",\"name\":\"x\",\"type\":\"chat\",\"lat_e6\":1}",                          // half a position
    "{" + k + ",\"name\":\"x\",\"type\":\"chat\",\"lat_e6\":91000000,\"lon_e6\":0}",
    "{\"key\":\"" + hexKey(1).substr(2) + "\",\"name\":\"x\",\"type\":\"chat\"}",         // a prefix, not a key
    "{\"key\":\"" + hexKey(1).substr(0, 62) + "zz\",\"name\":\"x\",\"type\":\"chat\"}",   // not hex
    "{\"name\":\"x\",\"type\":\"chat\"}",
  };
  for (const std::string& a : bad) EXPECT_TRUE(has(r.call("contact.put", a), "\"status\":\"invalid\"")) << a;
  EXPECT_EQ(0, r.t.count());
  EXPECT_EQ(0, r.t.writes);
}

TEST(ContactOps, AFullTableRefusesANewContactAndStillUpdatesAnExistingOne) {
  Rig r;
  for (int i = 0; i < FakeTable::CAP; i++) ASSERT_TRUE(has(r.put(i + 1, "c", "chat"), "\"ok\""));
  std::string res = r.put(0x77, "late", "chat");
  EXPECT_TRUE(has(res, "\"status\":\"failed\"") && has(res, "table-full")) << res;
  EXPECT_EQ(-1, r.t.find((const uint8_t*)std::string(32, '\x77').data()));
  EXPECT_TRUE(has(r.put(3, "renamed", "chat"), "\"changed\":true"));
}

TEST(ContactOps, RemoveSaysWhetherThereWasOneAndARetryIsOk) {
  Rig r;
  r.put(5, "x", "room");
  std::string res = r.call("contact.remove", "{\"key\":\"" + hexKey(5) + "\"}");
  EXPECT_TRUE(has(res, "\"removed\":true")) << res;
  EXPECT_EQ(0, r.t.count());
  int w = r.t.writes;
  res = r.call("contact.remove", "{\"key\":\"" + hexKey(5) + "\"}");
  EXPECT_TRUE(has(res, "\"status\":\"ok\"") && has(res, "\"removed\":false")) << res;
  EXPECT_EQ(w, r.t.writes);
  EXPECT_TRUE(has(r.call("contact.get", "{\"key\":\"" + hexKey(5) + "\"}"), "not-found"));
}

TEST(ContactOps, ProtectSetsAndClearsTheFlagAndIsIdempotent) {
  Rig r;
  r.put(5, "x", "repeater");
  const std::string k = "\"key\":\"" + hexKey(5) + "\"";
  std::string res = r.call("contact.protect", "{" + k + ",\"protected\":true}");
  EXPECT_TRUE(has(res, "\"fav\":true") && has(res, "\"changed\":true")) << res;
  int w = r.t.writes;
  res = r.call("contact.protect", "{" + k + ",\"protected\":true}");
  EXPECT_TRUE(has(res, "\"fav\":true") && has(res, "\"changed\":false")) << res;
  EXPECT_EQ(w, r.t.writes);
  res = r.call("contact.protect", "{" + k + ",\"protected\":false}");
  EXPECT_TRUE(has(res, "\"fav\":false") && has(res, "\"changed\":true")) << res;
  EXPECT_TRUE(has(r.call("contact.protect", "{\"key\":\"" + hexKey(9) + "\",\"protected\":true}"), "not-found"));
  EXPECT_TRUE(has(r.call("contact.protect", "{" + k + "}"), "\"invalid\""));
  EXPECT_TRUE(has(r.call("contact.protect", "{" + k + ",\"protected\":1}"), "\"invalid\""));
}

TEST(ContactOps, ListPagesByRecordAndEndsWithNull) {
  Rig r;
  for (int i = 0; i < 10; i++) r.put(i + 1, ("n" + std::to_string(i)).c_str(), "chat");
  std::string p1 = r.call("contact.list", "{}");
  EXPECT_TRUE(has(p1, "\"capacity\":12") && has(p1, "\"count\":10")) << p1;
  EXPECT_TRUE(has(p1, "\"name\":\"n5\"")) << p1;
  EXPECT_FALSE(has(p1, "\"name\":\"n6\"")) << "six per page: " << p1;
  EXPECT_TRUE(has(p1, "\"next\":6")) << p1;
  std::string p2 = r.call("contact.list", "{\"cursor\":6}");
  EXPECT_TRUE(has(p2, "\"name\":\"n6\"") && has(p2, "\"name\":\"n9\"")) << p2;
  EXPECT_TRUE(has(p2, "\"next\":null")) << "the last page says so: " << p2;
  EXPECT_TRUE(has(r.call("contact.list", "{\"cursor\":13}"), "\"invalid\""));
}

TEST(ContactOps, APageEndingExactlyAtTheLastContactHasNoNext) {
  Rig r;
  for (int i = 0; i < 6; i++) r.put(i + 1, "n", "chat");
  EXPECT_TRUE(has(r.call("contact.list", "{}"), "\"next\":null"));
}

TEST(ContactOps, AWalkSurvivesAddsAndRemovalsBetweenPages) {
  Rig r;
  for (int i = 0; i < 8; i++) r.put(i + 1, ("n" + std::to_string(i)).c_str(), "chat");
  std::string p1 = r.call("contact.list", "{}");   // n0..n5
  ASSERT_TRUE(has(p1, "\"next\":6")) << p1;
  r.call("contact.remove", "{\"key\":\"" + hexKey(2) + "\"}");   // behind the cursor: a hole, reused by the next add
  r.put(0x40, "late", "chat");                                    // takes the hole at record 1
  std::string p2 = r.call("contact.list", "{\"cursor\":6}");
  EXPECT_TRUE(has(p2, "\"name\":\"n6\"") && has(p2, "\"name\":\"n7\"")) << "nothing ahead of the cursor is skipped: " << p2;
  EXPECT_FALSE(has(p2, "\"name\":\"n0\"") || has(p2, "\"name\":\"n2\"")) << "nothing behind it is repeated: " << p2;
}

TEST(ContactOps, AListPageNeverOutgrowsTheResultWhateverTheNames) {
  Rig r;
  for (int i = 0; i < 12; i++) {
    int k = r.t.add("", 2, i + 1);
    memset(r.t.rec[k].name, '"', CONTACT_NAME_MAX);   // every byte escapes to two
    r.t.rec[k].has_pos = true;
    r.t.rec[k].lat_e6 = -89999999;
    r.t.rec[k].lon_e6 = -179999999;
    r.t.rec[k].last_advert = 4294967295u;
  }
  size_t seen = 0;
  std::string res;
  int cursor = 0;
  do {
    res = r.call("contact.list", "{\"cursor\":" + std::to_string(cursor) + "}");
    ASSERT_TRUE(has(res, "\"status\":\"ok\"")) << res;
    EXPECT_LE(res.size(), 1400u);
    for (size_t p = 0; (p = res.find("\"key\"", p)) != std::string::npos; p++) seen++;
    size_t nx = res.find("\"next\":");
    ASSERT_NE(std::string::npos, nx);
    if (has(res, "\"next\":null")) break;
    int nc = atoi(res.c_str() + nx + 7);
    ASSERT_GT(nc, cursor) << "a page must make progress";
    cursor = nc;
  } while (true);
  EXPECT_EQ(12u, seen);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
