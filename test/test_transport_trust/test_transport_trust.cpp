#include <gtest/gtest.h>
#include <stddef.h>
#include <stdint.h>
#include "helpers/BaseSerialInterface.h"

// A transport that says nothing about its trust: it must be treated as untrusted (fail closed).
class SilentTransport : public BaseSerialInterface {
public:
  void enable() override {}
  void disable() override {}
  bool isEnabled() const override { return true; }
  bool isConnected() const override { return true; }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t[], size_t len) override { return len; }
  size_t checkRecvFrame(uint8_t[]) override { return 0; }
};

class TrustedTransport : public SilentTransport {
public:
  bool isTrustedTransport() const override { return true; }
};

TEST(TransportTrust, UndeclaredTransportIsUntrusted) {
  SilentTransport t;
  EXPECT_FALSE(t.isTrustedTransport());
}

TEST(TransportTrust, DeclaredTransportIsTrusted) {
  TrustedTransport t;
  EXPECT_TRUE(t.isTrustedTransport());
}
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
