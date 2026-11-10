#include "core/log_tail.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

using namespace core;

namespace {
std::unique_ptr<LogTail::Storage> garbage() {
  auto s = std::make_unique<LogTail::Storage>();
  std::memset(s.get(), 0xA5, sizeof *s);  // like RTC memory after power-on
  return s;
}
}  // namespace

TEST(LogTail, NothingAfterPowerOnOrGarbage) {
  auto s = garbage();
  LogTail tail(*s);
  EXPECT_EQ(tail.take_previous(true), "");   // garbage fails the integrity check
  tail.append("hello\n");
  LogTail again(*s);
  EXPECT_EQ(again.take_previous(false), "");  // power-on: not trusted even if it looks intact
}

TEST(LogTail, KeepsThePreviousRunAcrossAReset) {
  auto s = garbage();
  {
    LogTail tail(*s);
    tail.take_previous(true);
    tail.append("line 1\nline 2\n");
  }
  LogTail after_reset(*s);
  EXPECT_EQ(after_reset.take_previous(true), "line 1\nline 2\n");
  EXPECT_EQ(after_reset.take_previous(true), "");  // taken once, then the new run starts empty
}

TEST(LogTail, WrapsKeepingWholeNewestLines) {
  auto s = garbage();
  LogTail tail(*s);
  tail.take_previous(true);
  const std::string line(99, 'x');
  for (int i = 0; i < 100; ++i) tail.append(line + "\n");  // 10 000 bytes into 6 144
  LogTail after(*s);
  const std::string prev = after.take_previous(true);
  EXPECT_LE(prev.size(), LogTail::kCapacity);
  EXPECT_EQ(prev.size() % 100, 0u);  // only whole lines
  EXPECT_EQ(prev.front(), 'x');
}

TEST(LogTail, ScrubsBinaryBytesFromAnInterruptedWrite) {
  auto s = garbage();
  LogTail tail(*s);
  tail.take_previous(true);
  tail.append(std::string("ok\x01\x1b[31m\n", 9));
  LogTail after(*s);
  EXPECT_EQ(after.take_previous(true), "ok??[31m\n");
}
