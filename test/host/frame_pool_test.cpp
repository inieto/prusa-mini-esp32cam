#include "core/frame_pool.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "fakes.hpp"

using core::FramePool;
using core::FrameRef;

TEST(FramePool, SlotReturnsWhenLastReferenceDies) {
  fakes::PoolStorage storage(2, 16);
  FramePool pool(storage.spans);
  FrameRef a = std::move(*pool.acquire()).commit(4, {});
  FrameRef b = std::move(*pool.acquire()).commit(4, {});
  EXPECT_FALSE(pool.acquire().has_value());  // both slots taken
  EXPECT_EQ(pool.in_use(), 2u);

  FrameRef a2 = a;  // shared
  a = FrameRef{};
  EXPECT_EQ(pool.in_use(), 2u);
  a2 = FrameRef{};
  EXPECT_EQ(pool.in_use(), 1u);
  EXPECT_TRUE(pool.acquire().has_value());
}

TEST(FramePool, UncommittedWriterFreesSlot) {
  fakes::PoolStorage storage(1, 16);
  FramePool pool(storage.spans);
  { auto w = pool.acquire(); ASSERT_TRUE(w.has_value()); }
  EXPECT_EQ(pool.in_use(), 0u);
}

TEST(FramePool, CommitClampsLengthAndKeepsInfo) {
  fakes::PoolStorage storage(1, 8);
  FramePool pool(storage.spans);
  FrameRef f = std::move(*pool.acquire()).commit(100, {.width = 640, .height = 480});
  EXPECT_EQ(f.bytes().size(), 8u);
  EXPECT_EQ(f.info().width, 640);
}

// Many threads sharing and dropping frames concurrently (run under ASan/UBSan).
TEST(FramePool, ConcurrentSharingNeverLeaksOrDoubleFrees) {
  fakes::PoolStorage storage(3, 64);
  FramePool pool(storage.spans);
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&pool] {
      for (int i = 0; i < 2000; ++i) {
        if (auto w = pool.acquire()) {
          FrameRef f = std::move(*w).commit(8, {});
          FrameRef copies[3] = {f, f, f};
          (void)copies;
        }
      }
    });
  }
  for (auto& th : threads) th.join();
  EXPECT_EQ(pool.in_use(), 0u);
}
