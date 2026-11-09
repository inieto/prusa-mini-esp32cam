#include "core/storage_policy.hpp"

#include <gtest/gtest.h>

using namespace core;

namespace {
constexpr CivilTime kNoon{2026, 11, 9, 12, 0, 5};
constexpr Millis kSec{1000};
}  // namespace

TEST(StoragePolicy, SanitizesPaths) {
  EXPECT_EQ(sanitize_relative_path(""), "");
  EXPECT_EQ(sanitize_relative_path("/"), "");
  EXPECT_EQ(sanitize_relative_path("/timelapse//2026-11-09/./a.jpg"), "timelapse/2026-11-09/a.jpg");
  EXPECT_EQ(sanitize_relative_path("../etc"), std::nullopt);
  EXPECT_EQ(sanitize_relative_path("a/../../b"), std::nullopt);
  EXPECT_EQ(sanitize_relative_path("a\\b"), std::nullopt);
  EXPECT_EQ(sanitize_relative_path(std::string("a\0b", 3)), std::nullopt);
  EXPECT_EQ(sanitize_relative_path("c:x"), std::nullopt);
  EXPECT_EQ(sanitize_relative_path(std::string(200, 'a')), std::nullopt);
}

TEST(StoragePolicy, TimelapseOffOrUndatedSavesNothing) {
  TimelapsePolicy off;
  EXPECT_FALSE(off.on_frame(Millis{0}, {}, kNoon));
  TimelapsePolicy always({.timelapse = TimelapseMode::Always});
  EXPECT_FALSE(always.on_frame(Millis{0}, {}, std::nullopt));
}

TEST(StoragePolicy, TimelapseAlwaysHonoursInterval) {
  TimelapsePolicy p({.timelapse = TimelapseMode::Always, .timelapse_interval_s = 60});
  auto first = p.on_frame(Millis{0}, {}, kNoon);
  ASSERT_TRUE(first);
  EXPECT_EQ(first->folder, "timelapse/2026-11-09");
  EXPECT_EQ(first->file, "120005.jpg");
  EXPECT_FALSE(p.on_frame(30 * kSec, {}, kNoon));
  EXPECT_TRUE(p.on_frame(60 * kSec, {}, kNoon));
}

TEST(StoragePolicy, TimelapseWhilePrintingUsesOneFolderPerJob) {
  TimelapsePolicy p({.timelapse = TimelapseMode::WhilePrinting, .timelapse_interval_s = 10});
  PrinterJob job{.phase = PrinterPhase::Idle};
  EXPECT_FALSE(p.on_frame(Millis{0}, job, kNoon));

  job = {.phase = PrinterPhase::Printing, .job_id = 7, .file = "Benchy 0.2mm.bgcode"};
  auto a = p.on_frame(Millis{0}, job, kNoon);
  ASSERT_TRUE(a);
  EXPECT_EQ(a->folder, "timelapse/2026-11-09_1200_Benchy_0_2mm_bgcode");

  CivilTime later = kNoon;
  later.minute = 30;
  auto b = p.on_frame(20 * kSec, job, later);
  ASSERT_TRUE(b);
  EXPECT_EQ(b->folder, a->folder);  // same print, same folder even as the clock moves

  job.phase = PrinterPhase::Paused;
  EXPECT_FALSE(p.on_frame(40 * kSec, job, later));

  job = {.phase = PrinterPhase::Printing, .job_id = 8, .file = ""};
  auto c = p.on_frame(60 * kSec, job, later);
  ASSERT_TRUE(c);
  EXPECT_EQ(c->folder, "timelapse/2026-11-09_1230");
}

TEST(StoragePolicy, RotatesOldestLogsAndKeepsUndated) {
  EXPECT_TRUE(logs_to_delete({"2026-11-01.log", "2026-11-02.log"}, 7).empty());
  auto del = logs_to_delete({"2026-11-03.log", "undated.log", "2026-11-01.log", "notes.txt",
                             "2026-11-02.log"},
                            2);
  EXPECT_EQ(del, std::vector<std::string>{"2026-11-01.log"});
  EXPECT_EQ(log_file_name(kNoon), "2026-11-09.log");
  EXPECT_EQ(log_file_name(std::nullopt), "undated.log");
}

TEST(StoragePolicy, ValidatesSettings) {
  EXPECT_TRUE(valid(StorageSettings{}));
  EXPECT_FALSE(valid(StorageSettings{.timelapse_interval_s = 5}));
  EXPECT_EQ(timelapse_mode_from("printing"), TimelapseMode::WhilePrinting);
  EXPECT_EQ(timelapse_mode_from("sometimes"), std::nullopt);
}

TEST(StoragePolicy, DecodesQueryValues) {
  EXPECT_EQ(url_decode("timelapse%2F2026-11-09%20a+b.jpg"), "timelapse/2026-11-09 a b.jpg");
  EXPECT_EQ(url_decode("%e2%82%ac"), "\xe2\x82\xac");
  EXPECT_EQ(url_decode("bad%2"), std::nullopt);
  EXPECT_EQ(url_decode("bad%zz"), std::nullopt);
  EXPECT_EQ(url_decode("nul%00"), std::nullopt);
}
