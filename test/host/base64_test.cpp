#include "core/base64.hpp"

#include <gtest/gtest.h>

namespace {

std::string enc(std::string_view s) {
  return core::base64_encode({reinterpret_cast<const uint8_t*>(s.data()), s.size()});
}

TEST(Base64, Rfc4648Vectors) {
  EXPECT_EQ(enc(""), "");
  EXPECT_EQ(enc("f"), "Zg==");
  EXPECT_EQ(enc("fo"), "Zm8=");
  EXPECT_EQ(enc("foo"), "Zm9v");
  EXPECT_EQ(enc("foob"), "Zm9vYg==");
  EXPECT_EQ(enc("fooba"), "Zm9vYmE=");
  EXPECT_EQ(enc("foobar"), "Zm9vYmFy");
}

}  // namespace
