// The console line assembler (src/helpers/LineAssembler.h): line ends, the buffer bound, over-long lines, reset.
#include <gtest/gtest.h>
#include <string>
#include "helpers/LineAssembler.h"

namespace {
struct Fed {
  std::string line;      // the last LINE
  int lines = 0, too_long = 0;
};

// Feeds `bytes` into an 8-byte buffer (7 characters and the NUL) with a canary after it.
Fed feedAll(LineAssembler& a, char* buf, const std::string& bytes) {
  Fed f;
  for (char c : bytes) {
    switch (a.feed(buf, 8, c)) {
      case LineAssembler::LINE: f.line = buf; f.lines++; buf[0] = 0; break;   // as the caller does
      case LineAssembler::TOO_LONG: f.too_long++; break;
      default: break;
    }
  }
  return f;
}
}  // namespace

TEST(LineAssembler, CrOrLfEndsALineAndCrLfIsOneEnd) {
  LineAssembler a; char buf[9] = {0};
  Fed f = feedAll(a, buf, "ver\r\nget\nab\r");
  EXPECT_EQ(3, f.lines);
  EXPECT_EQ("ab", f.line);
  EXPECT_EQ(0, f.too_long);
}

TEST(LineAssembler, ALineThatFillsTheBufferIsKept) {
  LineAssembler a; char buf[9] = {0};
  Fed f = feedAll(a, buf, "1234567\r");
  EXPECT_EQ(1, f.lines);
  EXPECT_EQ("1234567", f.line);
}

TEST(LineAssembler, AnOverLongLineIsDroppedToItsEndAndItsTailNeverRuns) {
  LineAssembler a; char buf[9] = {0};
  buf[8] = 'Z';   // canary past the 8-byte buffer
  Fed f = feedAll(a, buf, "12345678erase\r\nver\r");
  EXPECT_EQ(1, f.too_long);
  EXPECT_EQ(1, f.lines) << "only the line after the over-long one";
  EXPECT_EQ("ver", f.line);
  EXPECT_EQ('Z', buf[8]) << "nothing written past the buffer";
}

TEST(LineAssembler, TheBufferStaysTerminatedWhateverArrives) {
  LineAssembler a; char buf[9] = {0};
  buf[8] = 'Z';
  for (int i = 0; i < 2000; i++) {
    const char c = (i % 37 == 0) ? '\r' : (i % 53 == 0) ? '\n' : (char)('a' + i % 26);
    if (a.feed(buf, 8, c) == LineAssembler::LINE) buf[0] = 0;
    ASSERT_LT(strnlen(buf, 8), 8u) << "at byte " << i;
    ASSERT_EQ('Z', buf[8]);
  }
}

TEST(LineAssembler, ResetStartsAnEmptyLine) {
  LineAssembler a; char buf[9] = {0};
  feedAll(a, buf, "set na");   // a previous client's partial line
  a.reset();
  Fed f = feedAll(a, buf, "login x\r");
  EXPECT_EQ("login x", f.line);
}

TEST(LineAssembler, ResetEndsADrop) {
  LineAssembler a; char buf[9] = {0};
  feedAll(a, buf, "123456789");   // over-long, no end yet
  a.reset();
  Fed f = feedAll(a, buf, "ver\r");
  EXPECT_EQ(0, f.too_long);
  EXPECT_EQ("ver", f.line);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
