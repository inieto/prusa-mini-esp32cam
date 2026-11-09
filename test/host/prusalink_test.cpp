#include "core/prusalink.hpp"

#include <gtest/gtest.h>

using namespace core;

TEST(PrusaLink, ParsesStatusWhilePrinting) {
  auto job = prusalink::parse_status(R"({
    "job": {"id": 297, "progress": 45.5, "time_remaining": 3120, "time_printing": 1800},
    "storage": {"path": "/usb/", "name": "usb", "read_only": false},
    "printer": {"state": "PRINTING", "temp_nozzle": 215.1, "temp_bed": 60.0}})");
  ASSERT_TRUE(job);
  EXPECT_EQ(job->phase, PrinterPhase::Printing);
  EXPECT_EQ(job->job_id, 297u);
  EXPECT_FLOAT_EQ(job->progress, 45.5f);
  EXPECT_EQ(job->time_left_s, 3120u);
}

TEST(PrusaLink, ParsesIdleStatusWithoutJob) {
  auto job = prusalink::parse_status(R"({"printer": {"state": "IDLE"}})");
  ASSERT_TRUE(job);
  EXPECT_EQ(job->phase, PrinterPhase::Idle);
  EXPECT_EQ(job->job_id, 0u);
  EXPECT_FALSE(prusalink::parse_status(R"({"job": {}})"));
  EXPECT_FALSE(prusalink::parse_status("<html>"));
  EXPECT_EQ(prusalink::phase_from_state("ATTENTION"), PrinterPhase::Error);
  EXPECT_EQ(prusalink::phase_from_state("STOPPED"), PrinterPhase::Finished);
  EXPECT_EQ(prusalink::phase_from_state("NEW_STATE"), PrinterPhase::Unknown);
}

TEST(PrusaLink, TakesDisplayNameFromJob) {
  PrinterJob job;
  ASSERT_TRUE(prusalink::parse_job_file(
      R"({"id": 297, "state": "PRINTING", "progress": 45.5, "time_printing": 1800,
          "file": {"name": "BENCHY~1.BGC", "display_name": "Benchy 0.2mm PLA.bgcode",
                   "path": "/usb", "m_timestamp": 1700000000}})",
      job));
  EXPECT_EQ(job.file, "Benchy 0.2mm PLA.bgcode");
  EXPECT_FALSE(prusalink::parse_job_file(R"({"id": 1, "serial_print": true})", job));
}

TEST(PrintActivity, FailsOpenAndKeepsAGracePeriod) {
  constexpr Millis kMin{60'000};
  PrintActivity a(5 * kMin);
  EXPECT_FALSE(a.active(PrinterPhase::Idle, Millis{0}));  // never printed: idle is idle
  EXPECT_TRUE(a.active(PrinterPhase::Unknown, Millis{0}));
  EXPECT_TRUE(a.active(PrinterPhase::Printing, 10 * kMin));
  EXPECT_TRUE(a.active(PrinterPhase::Finished, 14 * kMin));
  EXPECT_FALSE(a.active(PrinterPhase::Finished, 16 * kMin));
}
