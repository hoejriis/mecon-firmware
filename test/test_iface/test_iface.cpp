#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/iface/IfaceDispatch.h"
#include "helpers/iface/IfaceFrames.h"

using namespace Iface;

static Status parse(const std::string& s, Request& r) {
  static RequestDoc doc;
  return parseRequest(doc, s.data(), s.size(), r);
}
static Status parse(const std::string& s) { Request r; return parse(s, r); }

// ---- envelope: every status path

TEST(IfaceEnvelope, ValidRequestParses) {
  Request r;
  ASSERT_EQ(ST_OK, parse(R"({"v":1,"id":"r-17","op":"config.get","args":{"keys":["name"]}})", r));
  EXPECT_STREQ("r-17", r.id);
  EXPECT_STREQ("config.get", r.op);
  EXPECT_TRUE(r.has_id);
  EXPECT_GE(r.args, 0);
}

TEST(IfaceEnvelope, ArgsAreOptionalAndMustBeAnObject) {
  Request r;
  ASSERT_EQ(ST_OK, parse(R"({"v":1,"id":"a","op":"hello"})", r));
  EXPECT_EQ(-1, r.args);
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","op":"hello","args":[1]})"));
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","op":"hello","args":"x"})"));
}

TEST(IfaceEnvelope, UnsupportedVersionKeepsTheId) {
  Request r;
  EXPECT_EQ(ST_UNSUPPORTED_VERSION, parse(R"({"v":2,"id":"a","op":"hello"})", r));
  EXPECT_TRUE(r.has_id);
  EXPECT_EQ(ST_UNSUPPORTED_VERSION, parse(R"({"v":0,"id":"a","op":"hello"})"));
  EXPECT_EQ(ST_INVALID, parse(R"({"v":"1","id":"a","op":"hello"})"));
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1.0,"id":"a","op":"hello"})"));
  EXPECT_EQ(ST_INVALID, parse(R"({"id":"a","op":"hello"})"));
}

TEST(IfaceEnvelope, AuthPresentIsRefusedWhateverItHolds) {
  EXPECT_EQ(ST_REFUSED, parse(R"({"v":1,"id":"a","op":"hello","auth":{"sig":"x"}})"));
  EXPECT_EQ(ST_REFUSED, parse(R"({"v":1,"id":"a","op":"hello","auth":null})"));
  // the version check outranks it: an unknown major executes and authorizes nothing
  EXPECT_EQ(ST_UNSUPPORTED_VERSION, parse(R"({"v":9,"id":"a","op":"hello","auth":1})"));
}

TEST(IfaceEnvelope, UnknownTopLevelFieldsAreIgnored) {
  EXPECT_EQ(ST_OK, parse(R"({"v":1,"id":"a","op":"hello","future":{"x":[1,2]},"y":true})"));
}

TEST(IfaceEnvelope, MissingOrBadOpIsInvalid) {
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a"})"));
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","op":""})"));
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","op":"he llo"})"));
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","op":5})"));
  EXPECT_EQ(ST_OK, parse(R"({"v":1,"id":"a","op":"a2345678901234567890123456789012"})"));      // 32
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","op":"a23456789012345678901234567890123"})"));  // 33
}

// ---- id charset and length

TEST(IfaceEnvelope, IdLengthBounds) {
  auto req = [](const std::string& id) { return R"({"v":1,"id":")" + id + R"(","op":"hello"})"; };
  EXPECT_EQ(ST_INVALID, parse(req("")));
  EXPECT_EQ(ST_OK, parse(req("x")));
  EXPECT_EQ(ST_OK, parse(req(std::string(32, 'x'))));
  Request r;
  EXPECT_EQ(ST_INVALID, parse(req(std::string(33, 'x')), r));
  EXPECT_FALSE(r.has_id);
}

TEST(IfaceEnvelope, IdCharset) {
  for (const char* good : {"aZ09", "a.b", "a_b", "a:b", "a-b", "0"})
    EXPECT_EQ(ST_OK, parse(std::string(R"({"v":1,"id":")") + good + R"(","op":"hello"})")) << good;
  for (const char* bad : {"a b", "a/b", "a\\u0041", "a\"", "a=b", "é", "a,b", "a;b", "a#"}) {
    std::string m = std::string(R"({"v":1,"id":")") + bad + R"(","op":"hello"})";
    if (std::string(bad) == "a\"") m = R"({"v":1,"id":"a\"","op":"hello"})";
    EXPECT_EQ(ST_INVALID, parse(m)) << bad;
  }
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":7,"op":"hello"})"));
}

// ---- malformed JSON

TEST(IfaceJson, TrailingDataIsRejected) {
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","op":"hello"} x)"));
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","op":"hello"}{"v":1})"));
  EXPECT_EQ(ST_OK, parse("  {\"v\":1,\"id\":\"a\",\"op\":\"hello\"} \r\n\t"));
  std::string nul = R"({"v":1,"id":"a","op":"hello"})";
  nul += '\0'; nul += "tail";
  EXPECT_EQ(ST_INVALID, parse(nul));
}

TEST(IfaceJson, DepthLimit) {
  RequestDoc d;
  std::string ok4 = R"({"a":{"b":{"c":{}}}})";           // 4 containers
  std::string bad5 = R"({"a":{"b":{"c":{"d":{}}}}})";    // 5
  EXPECT_EQ(J_OK, jsonParse(d, ok4.data(), ok4.size()));
  EXPECT_EQ(J_TOO_DEEP, jsonParse(d, bad5.data(), bad5.size()));
  std::string arr = R"({"a":[[[1]]]})";
  EXPECT_EQ(J_OK, jsonParse(d, arr.data(), arr.size()));
  std::string arr5 = R"({"a":[[[[1]]]]})";
  EXPECT_EQ(J_TOO_DEEP, jsonParse(d, arr5.data(), arr5.size()));
  // siblings do not accumulate depth
  std::string sib = R"({"a":{"x":{}},"b":{"x":{}},"c":[{},{}]})";
  EXPECT_EQ(J_OK, jsonParse(d, sib.data(), sib.size()));
}

static std::string arrayOf(int n) {   // root + key + array + n numbers = 3 + n tokens
  std::string s = R"({"a":[)";
  for (int i = 0; i < n; i++) s += (i ? ",1" : "1");
  return s + "]}";
}

TEST(IfaceJson, TokenLimit) {
  RequestDoc d;
  std::string at = arrayOf(MAX_TOKENS - 3), over = arrayOf(MAX_TOKENS - 2);
  EXPECT_EQ(J_OK, jsonParse(d, at.data(), at.size()));
  EXPECT_EQ(MAX_TOKENS, d.ntok);
  EXPECT_EQ(J_TOO_MANY_TOKENS, jsonParse(d, over.data(), over.size()));
}

TEST(IfaceJson, SizeLimit) {
  RequestDoc d;
  std::string pad = R"({"a":")" + std::string(MAX_MSG - 8, 'x') + R"("})";   // exactly 4096
  ASSERT_EQ(MAX_MSG, pad.size());
  EXPECT_EQ(J_OK, jsonParse(d, pad.data(), pad.size()));
  pad.insert(pad.size() - 2, "x");
  EXPECT_EQ(J_TOO_LONG, jsonParse(d, pad.data(), pad.size()));
  EXPECT_EQ(J_TOO_LONG, jsonParse(d, "", 0));
}

TEST(IfaceJson, DuplicateKeys) {
  RequestDoc d;
  for (const char* m : {R"({"id":"a","id":"b"})", R"({"v":1,"v":1})", R"({"a":{"k":1,"k":2}})", R"({"x":1,"y":2,"x":3})"})
    EXPECT_EQ(J_DUP_KEY, jsonParse(d, m, strlen(m))) << m;
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","id":"b","op":"hello"})"));
  EXPECT_EQ(ST_INVALID, parse(R"({"v":1,"id":"a","op":"hello","op":"x"})"));
  const char* same = R"({"a":{"k":1},"b":{"k":2}})";   // the same key in different objects is fine
  EXPECT_EQ(J_OK, jsonParse(d, same, strlen(same)));
}

TEST(IfaceJson, ControlCharactersInStrings) {
  RequestDoc d;
  std::string m = "{\"a\":\"x\ty\"}";
  EXPECT_EQ(J_CTRL, jsonParse(d, m.data(), m.size()));
  m = "{\"a\":\"x\ny\"}";
  EXPECT_EQ(J_CTRL, jsonParse(d, m.data(), m.size()));
  m = "{\"a\x01\":1}";
  EXPECT_EQ(J_CTRL, jsonParse(d, m.data(), m.size()));
  m = "{\"a\":\"x\\ty\"}";   // an escaped tab is fine to parse...
  EXPECT_EQ(J_OK, jsonParse(d, m.data(), m.size()));
  char out[16];
  EXPECT_FALSE(jsonGetString(d, jsonFind(d, 0, "a"), out, sizeof out, CS_ANY));   // ...but decodes to a control byte
  EXPECT_EQ(ST_INVALID, parse("{\"v\":1,\"id\":\"a\x01\",\"op\":\"hello\"}"));
}

TEST(IfaceJson, SyntaxAndRootErrors) {
  for (const char* m : {"{", "{\"a\":}", "{\"a\":1,}", "[1]", "\"x\"", "12", "{'a':1}", "{\"a\":1", "nul", "{\"a\":tru}", "{\"a\":1 \"b\":2}", "{\"a\" 1}", "{,\"a\":1}", "{\"a\":[,1]}", "{\"a\":[1,]}", "{\"a\":01}", "{\"a\":-}", "{\"a\":1.}", "{\"a\":.5}", "{\"a\":\"\\q\"}", "{\"a\":\"\\u12\"}", "{1:2}", "{\"a\":+1}"}) {
    RequestDoc d;
    EXPECT_NE(J_OK, jsonParse(d, m, strlen(m))) << m;
  }
}

// ---- typed readers

TEST(IfaceJson, Readers) {
  RequestDoc d;
  const char* m = R"({"s":"a\"b","i":-12,"b":true,"n":null,"o":{"k":1},"l":[5,"x"],"big":1e3,"neg0":-0})";
  ASSERT_EQ(J_OK, jsonParse(d, m, strlen(m)));
  char out[8];
  ASSERT_TRUE(jsonGetString(d, jsonFind(d, 0, "s"), out, sizeof out, CS_ANY));
  EXPECT_STREQ("a\"b", out);
  EXPECT_FALSE(jsonGetString(d, jsonFind(d, 0, "s"), out, 3, CS_ANY));          // 3 chars need cap 4
  EXPECT_TRUE(jsonGetString(d, jsonFind(d, 0, "s"), out, 4, CS_ANY));
  EXPECT_FALSE(jsonGetString(d, jsonFind(d, 0, "s"), out, sizeof out, CS_NAME));  // the quote is outside the set
  EXPECT_FALSE(jsonGetString(d, jsonFind(d, 0, "s"), out, sizeof out, CS_ANY, 4));  // min length
  EXPECT_FALSE(jsonGetString(d, jsonFind(d, 0, "i"), out, sizeof out, CS_ANY));
  int64_t v;
  ASSERT_TRUE(jsonGetInt(d, jsonFind(d, 0, "i"), -12, 5, &v));
  EXPECT_EQ(-12, v);
  EXPECT_FALSE(jsonGetInt(d, jsonFind(d, 0, "i"), -11, 5, &v));
  EXPECT_FALSE(jsonGetInt(d, jsonFind(d, 0, "big"), 0, 100000, &v));
  EXPECT_FALSE(jsonGetInt(d, jsonFind(d, 0, "neg0"), -5, 5, &v));
  EXPECT_FALSE(jsonGetInt(d, jsonFind(d, 0, "s"), 0, 100, &v));
  bool b = false;
  ASSERT_TRUE(jsonGetBool(d, jsonFind(d, 0, "b"), &b));
  EXPECT_TRUE(b);
  EXPECT_FALSE(jsonGetBool(d, jsonFind(d, 0, "i"), &b));
  EXPECT_TRUE(jsonIsNull(d, jsonFind(d, 0, "n")));
  int o = jsonFind(d, 0, "o"), l = jsonFind(d, 0, "l");
  EXPECT_TRUE(isObject(d, o));
  EXPECT_TRUE(isArray(d, l));
  EXPECT_FALSE(isObject(d, l));
  EXPECT_EQ(1, ([&] { int64_t x = 0; jsonGetInt(d, jsonFind(d, o, "k"), 0, 9, &x); return (int)x; })());
  ASSERT_TRUE(jsonGetInt(d, jsonElem(d, l, 0), 0, 9, &v));
  EXPECT_EQ(5, v);
  ASSERT_TRUE(jsonGetString(d, jsonElem(d, l, 1), out, sizeof out, CS_NAME));
  EXPECT_STREQ("x", out);
  EXPECT_EQ(-1, jsonElem(d, l, 2));
  EXPECT_EQ(-1, jsonFind(d, 0, "missing"));
  EXPECT_FALSE(jsonGetString(d, jsonFind(d, 0, "s"), out, 0, CS_ANY));
}

TEST(IfaceJson, UnicodeEscapesAreNotAccepted) {
  RequestDoc d;
  const char* m = R"({"a":"\u0041"})";
  ASSERT_EQ(J_OK, jsonParse(d, m, strlen(m)));
  char out[8];
  EXPECT_FALSE(jsonGetString(d, jsonFind(d, 0, "a"), out, sizeof out, CS_ANY));
}

// ---- writer and serializers

TEST(IfaceWriter, EscapesAndNests) {
  char b[128];
  JsonWriter w(b, sizeof b);
  w.beginObject();
  w.key("s"); w.str("a\"b\\c\n\x01");
  w.key("n"); w.num(-5);
  w.key("a"); w.beginArray(); w.num(1); w.boolean(false); w.null(); w.endArray();
  w.key("big"); w.unum(18446744073709551615ULL);
  w.endObject();
  ASSERT_TRUE(w.ok());
  EXPECT_STREQ(R"({"s":"a\"b\\c\u000a\u0001","n":-5,"a":[1,false,null],"big":18446744073709551615})" + 0, b) << "escaping";
}

TEST(IfaceWriter, NeverTruncatesSilently) {
  const char* full = R"({"k":"0123456789"})";
  size_t need = strlen(full);
  char b[64];
  for (size_t cap = 1; cap <= need; cap++) {   // a cap of need holds the text but not its terminator
    JsonWriter w(b, cap);
    w.beginObject(); w.key("k"); w.str("0123456789"); w.endObject();
    EXPECT_FALSE(w.ok()) << cap;
  }
  JsonWriter w(b, need + 1);
  w.beginObject(); w.key("k"); w.str("0123456789"); w.endObject();
  EXPECT_TRUE(w.ok());
  EXPECT_STREQ(full, b);
  JsonWriter z(b, 0);
  z.beginObject();
  EXPECT_FALSE(z.ok());
}

TEST(IfaceWriter, MisuseFails) {
  char b[32];
  JsonWriter w(b, sizeof b);
  w.endObject();
  EXPECT_FALSE(w.ok());
  JsonWriter deep(b, sizeof b);
  for (int i = 0; i < 9; i++) deep.beginArray();
  EXPECT_FALSE(deep.ok());
}

TEST(IfaceEnvelope, ResultSerialization) {
  char b[200];
  int n = writeResult(b, sizeof b, "r-1", ST_OK, R"({"x":1})", nullptr);
  EXPECT_STREQ(R"({"v":1,"id":"r-1","status":"ok","result":{"x":1}})", b);
  EXPECT_EQ((int)strlen(b), n);
  writeResult(b, sizeof b, "r-1", ST_OK, nullptr, nullptr);
  EXPECT_STREQ(R"({"v":1,"id":"r-1","status":"ok","result":{}})", b);
  writeResult(b, sizeof b, "r-1", ST_FAILED, nullptr, "flash \"full\"");
  EXPECT_STREQ(R"({"v":1,"id":"r-1","status":"failed","error":"flash \"full\""})", b);
  struct { Status s; const char* name; } all[] = {{ST_REFUSED, "refused"}, {ST_INVALID, "invalid"}, {ST_UNKNOWN_OP, "unknown-op"},
    {ST_UNSUPPORTED_VERSION, "unsupported-version"}, {ST_BUSY, "busy"}};
  for (auto& a : all) {
    writeResult(b, sizeof b, "i", a.s, "{\"leak\":1}", nullptr);
    EXPECT_EQ(std::string(R"({"v":1,"id":"i","status":")") + a.name + "\"}", b);   // no result on a non-ok status
  }
  EXPECT_EQ(-1, writeResult(b, 10, "r-1", ST_OK, nullptr, nullptr));
  int full = writeResult(b, sizeof b, "r-1", ST_OK, R"({"x":1})", nullptr);
  EXPECT_EQ(-1, writeResult(b, full, "r-1", ST_OK, R"({"x":1})", nullptr));
  EXPECT_EQ(full, writeResult(b, full + 1, "r-1", ST_OK, R"({"x":1})", nullptr));
}

TEST(IfaceEnvelope, EventSerialization) {
  char b[200];
  writeEvent(b, sizeof b, "health", 42, 7, true, 1792843200, R"({"heap":1})");
  EXPECT_STREQ(R"({"v":1,"ev":"health","seq":42,"epoch":7,"ts":1792843200,"data":{"heap":1}})", b);
  writeEvent(b, sizeof b, "health", 0, 0, false, 0, nullptr);
  EXPECT_STREQ(R"({"v":1,"ev":"health","seq":0,"epoch":0,"ts":null,"data":{}})", b);   // null is not 0
  EXPECT_EQ(-1, writeEvent(b, 20, "health", 1, 1, true, 1, nullptr));
}

// ---- the queue's room check (the inventory pages wait on it): it answers exactly what push() would do

TEST(OutQueueRoom, EventFitsAgreesWithPushAtEveryFillLevel) {
  for (size_t len : {(size_t)100, (size_t)700, (size_t)1300, (size_t)1500}) {
    uint8_t store[2048];
    OutQueue q(store, sizeof(store));
    std::vector<uint8_t> msg(len, 'x');
    for (int i = 0; i < 12; i++) {
      const bool fits = q.eventFits(len);
      const OutQueue::Push p = q.push(OutQueue::EVENT, msg.data(), len);
      EXPECT_EQ(fits, p == OutQueue::PUSH_OK) << "len " << len << " step " << i;
    }
  }
  uint8_t store[2048];
  OutQueue q(store, sizeof(store));
  std::vector<uint8_t> big(1400, 'y');
  ASSERT_EQ(OutQueue::PUSH_OK, q.push(OutQueue::EVENT, big.data(), big.size()));
  EXPECT_FALSE(q.eventFits(1400)) << "a second page waits while the first is queued";
  q.clear();
  EXPECT_TRUE(q.eventFits(1400)) << "cleared on a disconnect";
  EXPECT_EQ(0, q.count());
}

// ---- strings that are not UTF-8 (a contact name cut at a byte limit, or hostile)

static std::string jsonOfBytes(const std::string& bytes) {
  char b[200];
  JsonWriter w(b, sizeof(b));
  w.str(bytes.data(), bytes.size());
  EXPECT_TRUE(w.ok());
  return std::string(b, w.size());
}

TEST(IfaceJson, ValidUtf8PassesThroughUnchanged) {
  EXPECT_EQ("\"\xc3\xa5se\"", jsonOfBytes("\xc3\xa5se"));                     // a 2-byte letter
  EXPECT_EQ("\"\xe2\x82\xac\"", jsonOfBytes("\xe2\x82\xac"));               // 3 bytes
  EXPECT_EQ("\"\xf0\x9f\x93\xa1\"", jsonOfBytes("\xf0\x9f\x93\xa1"));     // 4 bytes
}

TEST(IfaceJson, EachInvalidByteBecomesAReplacementCharacter) {
  EXPECT_EQ("\"ab\\ufffd\"", jsonOfBytes("ab\xc3"));                // a sequence cut at the end
  EXPECT_EQ("\"\\ufffdx\"", jsonOfBytes("\x80x"));                  // a stray continuation byte
  EXPECT_EQ("\"\\ufffd\\ufffd\"", jsonOfBytes("\xc0\xaf"));       // an overlong '/'
  EXPECT_EQ("\"\\ufffd\\ufffd\\ufffd\"", jsonOfBytes("\xed\xa0\x80"));   // a surrogate
  EXPECT_EQ("\"\\ufffd\"", jsonOfBytes("\xff"));
  EXPECT_EQ("\"\\ufffd\\ufffd\\ufffd\\ufffd\"", jsonOfBytes("\xf4\x90\x80\x80"));   // over U+10FFFF
  EXPECT_EQ("\"\\ufffd\\ufffdA\"", jsonOfBytes("\xe2\x82" "A"));   // a 3-byte start cut short: per byte
}

// ---- result id cache

TEST(IfaceCache, HitEvictionAndOversize) {
  ResultCache c;
  const char* d; size_t l;
  EXPECT_EQ(ResultCache::MISS, c.find("a", &d, &l));
  c.store("a", "RESULT-A", 8);
  ASSERT_EQ(ResultCache::HIT, c.find("a", &d, &l));
  EXPECT_EQ(std::string("RESULT-A"), std::string(d, l));

  const int S = ResultCache::SLOTS;
  for (int i = 0; i < S - 1; i++) c.store(("k" + std::to_string(i)).c_str(), "x", 1);   // all held, "a" is the oldest
  EXPECT_EQ(ResultCache::HIT, c.find("a", &d, &l));
  c.store("klast", "x", 1);                                                              // one more evicts it
  EXPECT_EQ(ResultCache::EXPIRED, c.find("a", &d, &l));                                  // gone, but remembered
  EXPECT_EQ(ResultCache::HIT, c.find("k0", &d, &l));
  EXPECT_EQ(ResultCache::HIT, c.find("klast", &d, &l));

  c.store("k0", "NEW", 3);   // same id overwrites in place and does not evict anybody
  ASSERT_EQ(ResultCache::HIT, c.find("k0", &d, &l));
  EXPECT_EQ(std::string("NEW"), std::string(d, l));
  EXPECT_EQ(ResultCache::HIT, c.find("k1", &d, &l));

  std::string exact(ResultCache::ENTRY_CAP, 'z'), over(ResultCache::ENTRY_CAP + 1, 'z');
  c.store("exact", exact.data(), exact.size());
  c.store("over", over.data(), over.size());
  ASSERT_EQ(ResultCache::HIT, c.find("exact", &d, &l));
  EXPECT_EQ(exact.size(), l);
  EXPECT_EQ(ResultCache::EXPIRED, c.find("over", &d, &l));
}

// ---- dispatcher

struct Probe { int ran = 0; int auth_calls = 0; bool allow = true; Priv last_priv = PRIV_READ; std::string last_op; };

static Probe* P(void* u) { return static_cast<Probe*>(u); }
static Status hOk(OpContext& c) { P(c.user)->ran++; c.out->key("x"); c.out->num(1); return ST_OK; }
static Status hFail(OpContext& c) { P(c.user)->ran++; c.error = "disk-full"; return ST_FAILED; }
static Status hBig(OpContext& c) { P(c.user)->ran++; c.out->key("big"); c.out->str(std::string(500, 'q').c_str()); return ST_OK; }
static Status hArgs(OpContext& c) {
  P(c.user)->ran++;
  int64_t v;
  if (!jsonGetInt(*c.doc, jsonFind(*c.doc, c.req->args, "n"), 0, 10, &v)) return ST_INVALID;
  c.out->key("n"); c.out->num(v * 2);
  return ST_OK;
}
static bool auth(void* u, const OpEntry& op, const Request&) {
  P(u)->auth_calls++; P(u)->last_priv = op.priv; P(u)->last_op = op.name;
  return P(u)->allow;
}
static const OpEntry TABLE[] = {
  {"read.ok", hOk, PRIV_READ, false}, {"write.fail", hFail, PRIV_WRITE, true}, {"read.big", hBig, PRIV_READ, false},
  {"read.args", hArgs, PRIV_READ, false}, {"write.ok", hOk, PRIV_WRITE, true}, {"write.big", hBig, PRIV_WRITE, true},
};
static const size_t NT = sizeof(TABLE) / sizeof(TABLE[0]);
static std::string run(Dispatcher& d, const std::string& m, ResultCache* c = nullptr, size_t cap = 600) {
  std::vector<char> out(cap);
  int n = d.handle(m.data(), m.size(), out.data(), cap, c);
  return n < 0 ? std::string("<overflow>") : std::string(out.data(), n);
}
static std::string req(const std::string& id, const std::string& op, const std::string& args = "") {
  return R"({"v":1,"id":")" + id + R"(","op":")" + op + "\"" + (args.empty() ? "" : R"(,"args":)" + args) + "}";
}

TEST(IfaceDispatch, OkAndHandlerSeesAuthorizationFirst) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  EXPECT_EQ(R"({"v":1,"id":"a","status":"ok","result":{"x":1}})", run(d, req("a", "read.ok")));
  EXPECT_EQ(1, p.ran);
  EXPECT_EQ(1, p.auth_calls);
  EXPECT_EQ(PRIV_READ, p.last_priv);
  EXPECT_EQ(R"({"v":1,"id":"b","status":"ok","result":{"n":8}})", run(d, req("b", "read.args", R"({"n":4})")));
  EXPECT_EQ(R"({"v":1,"id":"c","status":"invalid"})", run(d, req("c", "read.args", R"({"n":40})")));
}

TEST(IfaceDispatch, RefusedMeansTheHandlerNeverRuns) {
  Probe p; p.allow = false;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  EXPECT_EQ(R"({"v":1,"id":"a","status":"refused"})", run(d, req("a", "write.fail")));
  EXPECT_EQ(0, p.ran);
  EXPECT_EQ(1, p.auth_calls);
  EXPECT_EQ(PRIV_WRITE, p.last_priv);
  EXPECT_EQ(R"({"v":1,"id":"b","status":"refused"})", run(d, R"({"v":1,"id":"b","op":"read.ok","auth":{}})"));
  EXPECT_EQ(0, p.ran);
}

static const char* denyWhy(void*) { return "unauthenticated"; }

TEST(IfaceDispatch, ARefusalCarriesTheDenyReasonWhenOneIsSet) {
  Probe p; p.allow = false;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  d.setDenyReason(denyWhy);
  EXPECT_EQ(R"({"v":1,"id":"a","status":"refused","error":"unauthenticated"})", run(d, req("a", "write.ok")));
  EXPECT_EQ(0, p.ran);
  // a duplicate id that is no longer authorized says the same, and still runs nothing
  ResultCache cache;
  p.allow = true;
  EXPECT_EQ(R"({"v":1,"id":"k","status":"ok","result":{"x":1}})", run(d, req("k", "write.ok"), &cache));
  p.allow = false;
  EXPECT_EQ(R"({"v":1,"id":"k","status":"refused","error":"unauthenticated"})", run(d, req("k", "write.ok"), &cache));
  EXPECT_EQ(1, p.ran);
}

TEST(IfaceDispatch, NoAuthCallbackRefusesEverything) {
  Probe p;
  Dispatcher d(TABLE, 4, nullptr, nullptr, &p);
  EXPECT_EQ(R"({"v":1,"id":"a","status":"refused"})", run(d, req("a", "read.ok")));
  EXPECT_EQ(0, p.ran);
}

TEST(IfaceDispatch, UnknownOpIsNotAuthorizedOrRun) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  EXPECT_EQ(R"({"v":1,"id":"a","status":"unknown-op"})", run(d, req("a", "nope")));
  EXPECT_EQ(0, p.auth_calls);
  EXPECT_EQ(0, p.ran);
}

TEST(IfaceDispatch, ParseFailuresRunNothing) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  EXPECT_EQ(R"({"v":1,"id":"a","status":"unsupported-version"})", run(d, R"({"v":2,"id":"a","op":"read.ok"})"));
  EXPECT_EQ(R"({"v":1,"id":"","status":"invalid"})", run(d, "garbage"));
  EXPECT_EQ(0, p.ran);
  EXPECT_EQ(0, p.auth_calls);
}

TEST(IfaceDispatch, FailedCarriesTheHandlersError) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  EXPECT_EQ(R"({"v":1,"id":"a","status":"failed","error":"disk-full"})", run(d, req("a", "write.fail")));
  EXPECT_EQ(1, p.ran);
}

TEST(IfaceDispatch, ResultThatDoesNotFitIsFailedNotTruncated) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  EXPECT_EQ(R"({"v":1,"id":"a","status":"failed","error":"result-too-large"})", run(d, req("a", "read.big"), nullptr, 200));
  EXPECT_EQ("<overflow>", run(d, req("a", "read.big"), nullptr, 20));
}

TEST(IfaceDispatch, DuplicateIdReturnsTheStoredResultWithoutRunning) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  ResultCache c;
  std::string first = run(d, req("w1", "write.fail"), &c);
  EXPECT_EQ(1, p.ran);
  EXPECT_EQ(first, run(d, req("w1", "write.fail"), &c));
  EXPECT_EQ(1, p.ran);
  EXPECT_EQ(2, p.auth_calls);   // the duplicate is authorized again, then answered from the cache
  run(d, req("w2", "write.ok"), &c);
  EXPECT_EQ(2, p.ran);
  run(d, req("w2", "write.ok"), &c);
  EXPECT_EQ(2, p.ran);
}

// A read is not kept: running it again is harmless, and keeping it would spend the cache on results nobody needs.
TEST(IfaceDispatch, ADuplicateReadRunsAgain) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  ResultCache c;
  run(d, req("r1", "read.ok"), &c);
  run(d, req("r1", "read.ok"), &c);
  EXPECT_EQ(2, p.ran);
  const char* data; size_t len;
  EXPECT_EQ(ResultCache::MISS, c.find("r1", &data, &len));
}

// One cache for the device: the same write arriving over a second transport (a second dispatcher) is not run twice.
TEST(IfaceDispatch, TheSameWriteOverAnotherTransportRunsOnce) {
  Probe p;
  Dispatcher usb(TABLE, NT, auth, &p, &p), mqtt(TABLE, NT, auth, &p, &p);
  ResultCache shared;
  std::string a = run(usb, req("job-9", "write.ok"), &shared);
  EXPECT_EQ(a, run(mqtt, req("job-9", "write.ok"), &shared));
  EXPECT_EQ(1, p.ran);
}

// An id whose result was evicted is still remembered (its hash): the duplicate is failed / result-expired, and the
// operation does not run a second time. Only after 64 newer state-changing ids is it forgotten.
TEST(IfaceDispatch, AnEvictedIdIsExpiredNotRunAgain) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  ResultCache c;
  run(d, req("first", "write.ok"), &c);
  const int S = ResultCache::SLOTS;
  for (int i = 0; i < S; i++) run(d, req("id" + std::to_string(i), "write.ok"), &c);
  EXPECT_EQ(S + 1, p.ran);
  EXPECT_EQ(R"({"v":1,"id":"first","status":"failed","error":"result-expired"})", run(d, req("first", "write.ok"), &c));
  EXPECT_EQ(S + 1, p.ran);
  for (int i = S; i < ResultCache::HASHES; i++) run(d, req("id" + std::to_string(i), "write.ok"), &c);
  EXPECT_EQ(ResultCache::HASHES + 1, p.ran);
  run(d, req("first", "write.ok"), &c);   // 64 newer ids later: forgotten, so it is a new request
  EXPECT_EQ(ResultCache::HASHES + 2, p.ran);
}

// A duplicate is checked against the caller's authority now: authority withdrawn since the first run is not
// bypassed by replaying the id, and a refused duplicate does not consume the stored result.
TEST(IfaceDispatch, ADuplicateRechecksTheCallersAuthority) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  ResultCache c;
  std::string first = run(d, req("w", "write.ok"), &c);
  p.allow = false;
  EXPECT_EQ(R"({"v":1,"id":"w","status":"refused"})", run(d, req("w", "write.ok"), &c));
  p.allow = true;
  EXPECT_EQ(first, run(d, req("w", "write.ok"), &c));
  EXPECT_EQ(1, p.ran);
}

TEST(IfaceDispatch, OversizeResultIsNotCachedAndItsDuplicateIsExpired) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  ResultCache c;
  std::string r = run(d, req("big", "write.big"), &c, 1000);
  ASSERT_GT(r.size(), ResultCache::ENTRY_CAP);
  EXPECT_NE(std::string::npos, r.find("\"status\":\"ok\""));
  EXPECT_EQ(R"({"v":1,"id":"big","status":"failed","error":"result-expired"})", run(d, req("big", "write.big"), &c, 1000));
  EXPECT_EQ(1, p.ran);
}

TEST(IfaceDispatch, OnlyExecutedOrAuthoritativeResultsAreRemembered) {
  Probe p;
  Dispatcher d(TABLE, NT, auth, &p, &p);
  ResultCache c;
  const char* data; size_t len;
  run(d, req("u", "nope"), &c);
  EXPECT_EQ(ResultCache::MISS, c.find("u", &data, &len));
  run(d, R"({"v":2,"id":"v","op":"read.ok"})", &c);
  EXPECT_EQ(ResultCache::MISS, c.find("v", &data, &len));
  p.allow = false;
  run(d, req("r", "write.ok"), &c);   // refused: nothing ran, so it is evaluated again next time
  EXPECT_EQ(ResultCache::MISS, c.find("r", &data, &len));
  p.allow = true;
  run(d, req("f", "write.fail"), &c);
  EXPECT_EQ(ResultCache::HIT, c.find("f", &data, &len));
}

// ---- framing

static std::vector<uint8_t> msgOf(size_t n) {
  std::vector<uint8_t> m(n);
  for (size_t i = 0; i < n; i++) m[i] = (uint8_t)(i * 7 + 3);
  return m;
}
static std::vector<std::vector<uint8_t>> fragment(const std::vector<uint8_t>& m, uint8_t xid) {
  Fragmenter f;
  std::vector<std::vector<uint8_t>> frames;
  if (!f.begin(m.data(), m.size(), xid)) return frames;
  uint8_t b[FRAME_MAX];
  while (!f.done()) { size_t n = f.next(b); frames.emplace_back(b, b + n); }
  return frames;
}
static RxResult feed(Reassembler& r, const std::vector<uint8_t>& f, uint32_t t = 0) { return r.feed(f.data(), f.size(), t); }

TEST(IfaceFrames, RoundTripAtTheBoundaries) {
  for (size_t n : {size_t(1), size_t(168), size_t(169), size_t(336), size_t(337), size_t(4096)}) {
    auto m = msgOf(n);
    auto frames = fragment(m, 9);
    ASSERT_EQ((n + 167) / 168, frames.size()) << n;
    for (size_t i = 0; i < frames.size(); i++) {
      EXPECT_EQ(0x7F, frames[i][0]);
      EXPECT_EQ(9, frames[i][1]);
      EXPECT_EQ(i, frames[i][2]);
      EXPECT_EQ(frames.size(), frames[i][3]);
      EXPECT_LE(frames[i].size(), FRAME_MAX);
    }
    static Reassembler r;
    RxResult res = RX_INCOMPLETE;
    for (size_t i = 0; i < frames.size(); i++) {
      EXPECT_EQ(RX_INCOMPLETE, res) << n;
      res = feed(r, frames[i]);
    }
    ASSERT_EQ(RX_COMPLETE, res) << n;
    ASSERT_EQ(n, r.size());
    EXPECT_EQ(0, memcmp(m.data(), r.data(), n)) << n;
  }
}

TEST(IfaceFrames, OversizeAndEmptyAreNotFragmented) {
  Fragmenter f;
  auto m = msgOf(4097);
  EXPECT_FALSE(f.begin(m.data(), m.size(), 0));
  EXPECT_FALSE(f.begin(m.data(), 0, 0));
  EXPECT_EQ(25u, frameCount(4096));
  EXPECT_EQ(0u, frameCount(4097));
}

// An idle transport holds no reassembly buffer: it is allocated when a message starts and freed when taken.
TEST(IfaceFrames, ReassemblyBufferExistsOnlyWhileAMessageIsInFlight) {
  Reassembler r;
  EXPECT_EQ(nullptr, r.data());
  auto f = fragment(msgOf(400), 3);
  ASSERT_EQ(RX_INCOMPLETE, feed(r, f[0]));
  EXPECT_NE(nullptr, r.data());
  ASSERT_EQ(RX_INCOMPLETE, feed(r, f[1]));
  ASSERT_EQ(RX_COMPLETE, feed(r, f[2]));
  EXPECT_EQ(400u, r.size());
  r.release();
  EXPECT_EQ(nullptr, r.data());
  ASSERT_EQ(RX_INCOMPLETE, feed(r, f[0], 0));
  EXPECT_TRUE(r.tick(REASSEMBLY_TIMEOUT_MS));   // dropped by the timeout: freed too
  EXPECT_EQ(nullptr, r.data());
}

TEST(IfaceFrames, ReassemblerRejectsMalformedFrames) {
  static Reassembler r;
  auto ok = fragment(msgOf(200), 1);   // 2 frames
  auto bad = [&](std::vector<uint8_t> f) { return feed(r, f); };
  auto f0 = ok[0];
  auto t = f0; t[0] = 0x7E;           EXPECT_EQ(RX_MALFORMED, bad(t));       // wrong code
  t = f0; t[3] = 0;                   EXPECT_EQ(RX_MALFORMED, bad(t));       // count 0
  t = f0; t[3] = 26;                  EXPECT_EQ(RX_MALFORMED, bad(t));       // count 26
  t = f0; t[2] = 2;                   EXPECT_EQ(RX_MALFORMED, bad(t));       // index >= count
  t = f0; t.pop_back();               EXPECT_EQ(RX_MALFORMED, bad(t));       // non-final frame short
  t = {0x7F, 1, 0};                   EXPECT_EQ(RX_MALFORMED, bad(t));       // no header
  t = {0x7F, 1, 0, 1};                EXPECT_EQ(RX_MALFORMED, bad(t));       // no payload
  t = f0; t.resize(FRAME_MAX + 1);    EXPECT_EQ(RX_MALFORMED, bad(t));       // too long
  // a final frame that would push the total over 4096: 24 full frames + 169 is impossible by size, so use 25 x full
  std::vector<uint8_t> last(FRAME_HDR + 168, 0);
  last[0] = 0x7F; last[1] = 1; last[2] = 24; last[3] = 25;
  EXPECT_EQ(RX_MALFORMED, bad(last));                                        // 24*168+168 = 4200 > 4096
  last.resize(FRAME_HDR + 64);
  EXPECT_EQ(RX_INCOMPLETE, bad(last));                                       // 24*168+64 = 4096 is the maximum
  EXPECT_EQ(RX_MALFORMED, r.feed(nullptr, 5, 0));
  // none of the rejected frames disturbed the state: the real message still completes
  Reassembler r2;
  EXPECT_EQ(RX_INCOMPLETE, feed(r2, f0));
  t = f0; t[3] = 3;                   EXPECT_EQ(RX_MALFORMED, feed(r2, t));  // same xid, different count
  EXPECT_EQ(RX_COMPLETE, feed(r2, ok[1]));
  EXPECT_EQ(200u, r2.size());
}

TEST(IfaceFrames, OutOfOrderAndDuplicateFrames) {
  auto m = msgOf(700);   // 5 frames
  auto frames = fragment(m, 4);
  ASSERT_EQ(5u, frames.size());
  static Reassembler r;
  for (int i : {3, 0, 4, 1}) EXPECT_EQ(RX_INCOMPLETE, feed(r, frames[i])) << i;
  EXPECT_EQ(RX_INCOMPLETE, feed(r, frames[3]));   // a duplicate adds nothing
  EXPECT_EQ(RX_INCOMPLETE, feed(r, frames[0]));
  ASSERT_EQ(RX_COMPLETE, feed(r, frames[2]));
  ASSERT_EQ(700u, r.size());
  EXPECT_EQ(0, memcmp(m.data(), r.data(), 700));
  // a duplicate must not overwrite: a corrupted resend of an accepted frame is ignored
  Reassembler& r2 = r;
  feed(r2, frames[0]);
  auto evil = frames[0]; evil[FRAME_HDR] ^= 0xFF;
  EXPECT_EQ(RX_INCOMPLETE, feed(r2, evil));
  for (int i = 1; i < 4; i++) feed(r2, frames[i]);
  ASSERT_EQ(RX_COMPLETE, feed(r2, frames[4]));
  EXPECT_EQ(0, memcmp(m.data(), r2.data(), 700));
}

TEST(IfaceFrames, ANewXidDiscardsTheIncompleteMessage) {
  auto a = fragment(msgOf(400), 1), b = fragment(msgOf(300), 2);
  static Reassembler r;
  EXPECT_EQ(RX_INCOMPLETE, feed(r, a[0]));
  EXPECT_EQ(RX_INCOMPLETE, feed(r, b[0]));
  uint8_t x = 0;
  ASSERT_TRUE(r.takeDiscard(&x));
  EXPECT_EQ(1, x);
  EXPECT_FALSE(r.takeDiscard(&x));
  ASSERT_EQ(RX_COMPLETE, feed(r, b[1]));
  EXPECT_EQ(300u, r.size());
  // the old message's remaining frames now start a fresh (never completing) message rather than joining b
  EXPECT_EQ(RX_INCOMPLETE, feed(r, a[1]));
}

TEST(IfaceFrames, TimeoutAfterFiveSeconds) {
  auto m = msgOf(400);
  auto fr = fragment(m, 5);
  static Reassembler r;
  EXPECT_EQ(RX_INCOMPLETE, feed(r, fr[0], 1000));
  EXPECT_FALSE(r.tick(5999));
  EXPECT_TRUE(r.active());
  EXPECT_TRUE(r.tick(6000));
  EXPECT_FALSE(r.active());
  uint8_t x = 99;
  ASSERT_TRUE(r.takeDiscard(&x));
  EXPECT_EQ(5, x);
  // a late frame of the dead message is not joined to it
  EXPECT_EQ(RX_INCOMPLETE, feed(r, fr[1], 6001));
  EXPECT_EQ(RX_INCOMPLETE, feed(r, fr[0], 6002));
  EXPECT_EQ(RX_COMPLETE, feed(r, fr[2], 6003));
  // the same holds when no tick ran in between: feed() checks the clock itself
  Reassembler& q = r;
  feed(q, fr[0], 10000);
  EXPECT_EQ(RX_INCOMPLETE, feed(q, fr[1], 15000));
  EXPECT_EQ(RX_INCOMPLETE, feed(q, fr[0], 15001));
  EXPECT_EQ(RX_COMPLETE, feed(q, fr[2], 15002));
  // the clock wrapping around does not stall the timeout
  feed(q, fr[0], 0xFFFFFF00u);
  EXPECT_TRUE(q.tick(0xFFFFFF00u + 5000));
}

TEST(IfaceQueue, EventsDropAndResultsAreRefusedWhenFull) {
  uint8_t store[2048];
  OutQueue q(store, sizeof store);
  auto m = msgOf(10);
  for (int i = 0; i < QUEUE_MESSAGES - 1; i++) EXPECT_EQ(OutQueue::PUSH_OK, q.push(OutQueue::EVENT, m.data(), m.size())) << i;
  EXPECT_EQ(QUEUE_MESSAGES - 1, q.count());
  EXPECT_EQ(OutQueue::PUSH_DROPPED, q.push(OutQueue::EVENT, m.data(), m.size()));   // the last slot is a result's
  EXPECT_EQ(OutQueue::PUSH_DROPPED, q.push(OutQueue::EVENT, m.data(), m.size()));
  EXPECT_EQ(2u, q.droppedEvents());
  EXPECT_EQ(OutQueue::PUSH_OK, q.push(OutQueue::RESULT, m.data(), m.size()));
  EXPECT_EQ(QUEUE_MESSAGES, q.count());
  EXPECT_EQ(OutQueue::PUSH_BUSY, q.push(OutQueue::RESULT, m.data(), m.size()));
  EXPECT_EQ(1u, q.refusedResults());
  EXPECT_EQ(2u, q.droppedEvents());   // a refused result is not an event drop
  EXPECT_EQ(QUEUE_MESSAGES, q.count());
  uint8_t f[FRAME_MAX];
  ASSERT_GT(q.nextFrame(f), 0u);       // draining one slot makes room again
  EXPECT_EQ(OutQueue::PUSH_OK, q.push(OutQueue::RESULT, m.data(), m.size()));
}

// Events cannot take the bytes kept for results: with the queue full of events, a short result still fits.
TEST(IfaceQueue, EventsLeaveRoomForAResult) {
  uint8_t store[2048];
  OutQueue q(store, sizeof store);
  auto ev = msgOf(600), busy = msgOf(60);
  EXPECT_EQ(OutQueue::PUSH_OK, q.push(OutQueue::EVENT, ev.data(), ev.size()));
  EXPECT_EQ(OutQueue::PUSH_OK, q.push(OutQueue::EVENT, ev.data(), ev.size()));
  EXPECT_EQ(OutQueue::PUSH_DROPPED, q.push(OutQueue::EVENT, ev.data(), ev.size()));   // would eat into the reserve
  EXPECT_EQ(OutQueue::PUSH_OK, q.push(OutQueue::RESULT, busy.data(), busy.size()));
}

TEST(IfaceQueue, ByteStorageCountsAsFull) {
  uint8_t store[300];
  OutQueue q(store, sizeof store);
  auto m = msgOf(200);
  EXPECT_EQ(OutQueue::PUSH_OK, q.push(OutQueue::RESULT, m.data(), m.size()));
  EXPECT_EQ(OutQueue::PUSH_BUSY, q.push(OutQueue::RESULT, m.data(), m.size()));
  EXPECT_EQ(OutQueue::PUSH_DROPPED, q.push(OutQueue::EVENT, m.data(), m.size()));
  EXPECT_EQ(OutQueue::PUSH_BAD, q.push(OutQueue::RESULT, m.data(), 0));
  auto big = msgOf(4097);
  uint8_t bigstore[8192];
  OutQueue q2(bigstore, sizeof bigstore);
  EXPECT_EQ(OutQueue::PUSH_BAD, q2.push(OutQueue::RESULT, big.data(), big.size()));
}

TEST(IfaceQueue, FramesNeverInterleaveAndSurviveRingWrap) {
  uint8_t store[1000];   // small enough that messages wrap around the end
  OutQueue q(store, sizeof store);
  static Reassembler r;
  std::vector<std::vector<uint8_t>> sent;
  uint8_t f[FRAME_MAX];
  int delivered = 0;
  for (int round = 0; round < 40; round++) {
    size_t sizes[] = {1, 168, 169, 400, 333};
    auto m = msgOf(sizes[round % 5] + round);
    if (q.push(OutQueue::RESULT, m.data(), m.size()) == OutQueue::PUSH_OK) sent.push_back(m);
    if (round % 3 == 2 || round == 39) {
      // drain whole messages: every frame belongs to the one message being delivered
      for (;;) {
        size_t n = q.nextFrame(f);
        if (!n) break;
        RxResult res = r.feed(f, n, 0);
        ASSERT_NE(RX_MALFORMED, res);
        uint8_t dx; EXPECT_FALSE(r.takeDiscard(&dx)) << "a message was interleaved with another";
        if (res == RX_COMPLETE) {
          ASSERT_LT((size_t)delivered, sent.size());
          ASSERT_EQ(sent[delivered].size(), r.size());
          EXPECT_EQ(0, memcmp(sent[delivered].data(), r.data(), r.size())) << "message " << delivered;
          delivered++;
        }
      }
    }
  }
  EXPECT_GT(delivered, 10);
  EXPECT_EQ(0u, q.nextFrame(f));
}

TEST(IfaceQueue, XidAdvancesPerMessage) {
  uint8_t store[1024];
  OutQueue q(store, sizeof store);
  auto m = msgOf(5);
  q.push(OutQueue::EVENT, m.data(), 5);
  q.push(OutQueue::EVENT, m.data(), 5);
  uint8_t a[FRAME_MAX], b[FRAME_MAX];
  q.nextFrame(a); q.nextFrame(b);
  EXPECT_NE(a[1], b[1]);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
