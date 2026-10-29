// channel.list / get / put / remove over a fake native channel table (src/helpers/iface/ChannelOps.cpp,
// docs/contract/OPERATIONS.md section 5.1).
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <string.h>
#include <helpers/iface/ChannelOps.h>

using namespace Iface;

namespace {

struct FakeTable {
  static const int N = 40;
  std::string name[N];
  uint8_t key[N][CHANNEL_SECRET_BYTES] = {{0}};
  int writes = 0;
  bool fail = false;
};

bool getCb(void* u, int slot, ChannelInfo& out) {
  FakeTable* t = static_cast<FakeTable*>(u);
  if (slot < 0 || slot >= FakeTable::N) return false;
  snprintf(out.name, sizeof(out.name), "%s", t->name[slot].c_str());
  out.hash = (uint8_t)(t->key[slot][0] ^ 0x5A);   // stands in for the SHA-256 byte
  out.key_bytes = CHANNEL_SECRET_BYTES;
  return true;
}
bool setCb(void* u, int slot, const char* name, const uint8_t* key) {
  FakeTable* t = static_cast<FakeTable*>(u);
  if (t->fail || slot < 0 || slot >= FakeTable::N) return false;
  t->name[slot] = name;
  memcpy(t->key[slot], key, CHANNEL_SECRET_BYTES);
  t->writes++;
  return true;
}
int findCb(void* u, const uint8_t* key) {
  FakeTable* t = static_cast<FakeTable*>(u);
  for (int i = 0; i < FakeTable::N; i++) if (memcmp(t->key[i], key, CHANNEL_SECRET_BYTES) == 0) return i;
  return -1;
}
bool authAll(void*, const OpEntry&, const Request&) { return true; }

const OpEntry OPS[] = {
  {"channel.list", opChannelList, PRIV_READ, false},
  {"channel.get", opChannelGet, PRIV_READ, false},
  {"channel.put", opChannelPut, PRIV_WRITE, true},
  {"channel.remove", opChannelRemove, PRIV_WRITE, true},
};

const char* KEY_A = "00112233445566778899aabbccddeeff";
const char* KEY_B = "ffeeddccbbaa99887766554433221100";

struct Rig {
  FakeTable t;
  ChannelHost host{FakeTable::N, getCb, setCb, findCb, &t};
  Dispatcher d{OPS, 4, authAll, nullptr, &host};
  ResultCache cache;
  int n = 0;
  std::string call(const std::string& op, const std::string& args) {
    std::string m = "{\"v\":1,\"id\":\"t" + std::to_string(++n) + "\",\"op\":\"" + op + "\",\"args\":" + args + "}";
    std::vector<char> out(2048);
    int len = d.handle(m.data(), m.size(), out.data(), out.size(), &cache);
    return len < 0 ? "<overflow>" : std::string(out.data(), len);
  }
  std::string put(int slot, const char* name, const char* key) {
    return call("channel.put", "{\"slot\":" + std::to_string(slot) + ",\"name\":\"" + name + "\",\"secret\":\"" + key + "\"}");
  }
};

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST(ChannelOps, PutWritesTheNativeSlotAndReadsBackWithoutTheSecret) {
  Rig r;
  std::string res = r.put(3, "ops", KEY_A);
  EXPECT_TRUE(has(res, "\"status\":\"ok\"")) << res;
  EXPECT_TRUE(has(res, "\"slot\":3")) << res;
  EXPECT_TRUE(has(res, "\"replaced\":false")) << res;
  EXPECT_EQ("ops", r.t.name[3]);
  EXPECT_EQ(0x11, r.t.key[3][1]);
  std::string got = r.call("channel.get", "{\"slot\":3}");
  EXPECT_TRUE(has(got, "\"name\":\"ops\"")) << got;
  EXPECT_TRUE(has(got, "\"hash\":\"5a\"")) << got;   // key byte 0 is 0x00
  EXPECT_TRUE(has(got, "\"key_bits\":128")) << got;
  EXPECT_FALSE(has(got, "00112233")) << "the secret must never be in a result";
}

TEST(ChannelOps, TheKeyIsWrittenByteForByteAsItsHexReads) {
  Rig r;
  ASSERT_TRUE(has(r.put(1, "k", "0123456789abcdefFEDCBA9876543210"), "\"ok\""));   // no byte reads the same nibble-swapped
  const uint8_t want[CHANNEL_SECRET_BYTES] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                              0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
  EXPECT_EQ(0, memcmp(want, r.t.key[1], CHANNEL_SECRET_BYTES));
}

TEST(ChannelOps, ASecondPutToTheSameSlotIsAReplacement) {
  Rig r;
  r.put(3, "ops", KEY_A);
  std::string res = r.put(3, "ops2", KEY_B);
  EXPECT_TRUE(has(res, "\"replaced\":true")) << res;
  EXPECT_EQ("ops2", r.t.name[3]);
  EXPECT_EQ(0xff, r.t.key[3][0]);
}

TEST(ChannelOps, AKeyAlreadyInAnotherSlotIsRefusedAndNothingIsWritten) {
  Rig r;
  r.put(3, "ops", KEY_A);
  int before = r.t.writes;
  std::string res = r.put(5, "dup", KEY_A);
  EXPECT_TRUE(has(res, "\"status\":\"failed\"")) << res;
  EXPECT_TRUE(has(res, "duplicate-secret")) << res;
  EXPECT_EQ(before, r.t.writes);
  EXPECT_EQ("", r.t.name[5]);
}

TEST(ChannelOps, BadArgumentsAreInvalidAndWriteNothing) {
  Rig r;
  const std::string bad[] = {
    "{\"slot\":40,\"name\":\"x\",\"secret\":\"00112233445566778899aabbccddeeff\"}",   // past the table
    "{\"slot\":-1,\"name\":\"x\",\"secret\":\"00112233445566778899aabbccddeeff\"}",
    "{\"name\":\"x\",\"secret\":\"00112233445566778899aabbccddeeff\"}",               // no slot
    "{\"slot\":1,\"name\":\"\",\"secret\":\"00112233445566778899aabbccddeeff\"}",     // empty name
    "{\"slot\":1,\"name\":\"0123456789012345678901234567890123\",\"secret\":\"00112233445566778899aabbccddeeff\"}",
    "{\"slot\":1,\"name\":\"x\",\"secret\":\"0011223344556677\"}",                    // 64-bit key
    "{\"slot\":1,\"name\":\"x\",\"secret\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\"}",
    "{\"slot\":1,\"name\":\"x\",\"secret\":\"00112233445566778899aabbccddeefg\"}",    // not hex
    "{\"slot\":1,\"name\":\"x\",\"secret\":\"00000000000000000000000000000000\"}",    // an empty slot's key
    "{\"slot\":1,\"name\":\"x\"}",                                                     // no secret
  };
  for (const std::string& a : bad) {
    std::string res = r.call("channel.put", a);
    EXPECT_TRUE(has(res, "\"status\":\"invalid\"")) << a << " -> " << res;
  }
  EXPECT_EQ(0, r.t.writes);
}

TEST(ChannelOps, AFailedWriteIsReportedAsFailed) {
  Rig r;
  r.t.fail = true;
  std::string res = r.put(2, "ops", KEY_A);
  EXPECT_TRUE(has(res, "\"status\":\"failed\"")) << res;
  EXPECT_TRUE(has(res, "write-failed")) << res;
}

TEST(ChannelOps, RemoveClearsTheSlotAndSaysWhetherThereWasOne) {
  Rig r;
  r.put(4, "ops", KEY_A);
  std::string res = r.call("channel.remove", "{\"slot\":4}");
  EXPECT_TRUE(has(res, "\"removed\":true")) << res;
  EXPECT_EQ("", r.t.name[4]);
  uint8_t zero[CHANNEL_SECRET_BYTES] = {0};
  EXPECT_EQ(0, memcmp(zero, r.t.key[4], CHANNEL_SECRET_BYTES)) << "the key must go with the name";
  int before = r.t.writes;
  res = r.call("channel.remove", "{\"slot\":4}");
  EXPECT_TRUE(has(res, "\"removed\":false")) << res;
  EXPECT_EQ(before, r.t.writes) << "an empty slot is not written again";
  res = r.call("channel.get", "{\"slot\":4}");
  EXPECT_TRUE(has(res, "empty-slot")) << res;
}

TEST(ChannelOps, ListSkipsEmptySlotsAndPagesByUsedSlot) {
  Rig r;
  char key[33];
  for (int i = 0; i < 12; i++) {   // slots 0, 3, 6, ... 33
    snprintf(key, sizeof(key), "%02x112233445566778899aabbccddeeff", i + 1);
    ASSERT_TRUE(has(r.put(i * 3, ("ch" + std::to_string(i)).c_str(), key), "\"ok\""));
  }
  std::string p1 = r.call("channel.list", "{}");
  EXPECT_TRUE(has(p1, "\"capacity\":40")) << p1;
  EXPECT_TRUE(has(p1, "\"name\":\"ch7\"")) << p1;
  EXPECT_FALSE(has(p1, "\"name\":\"ch8\"")) << "eight per page";
  EXPECT_TRUE(has(p1, "\"next\":22")) << p1;   // after slot 21, the eighth
  std::string p2 = r.call("channel.list", "{\"cursor\":22}");
  EXPECT_TRUE(has(p2, "\"name\":\"ch8\"")) << p2;
  EXPECT_TRUE(has(p2, "\"name\":\"ch11\"")) << p2;
  EXPECT_TRUE(has(p2, "\"next\":null")) << p2;
  EXPECT_FALSE(has(p1 + p2, "112233445566")) << "no secret in a list";
  EXPECT_TRUE(has(r.call("channel.list", "{\"cursor\":41}"), "\"invalid\""));
}

TEST(ChannelOps, GetOfASlotOutsideTheTableIsInvalid) {
  Rig r;
  EXPECT_TRUE(has(r.call("channel.get", "{\"slot\":40}"), "\"invalid\""));
  EXPECT_TRUE(has(r.call("channel.get", "{}"), "\"invalid\""));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
