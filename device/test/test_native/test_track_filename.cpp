#include <gtest/gtest.h>

#include "storage/track_filename.h"

// --- Validation ---

TEST(TrackFilename, ValidFilenameAccepted) {
  EXPECT_TRUE(TrackFilename::isValid("/gps_20260331_143000.csv"));
}

TEST(TrackFilename, RejectsFilenameWithoutGpsPrefix) {
  EXPECT_FALSE(TrackFilename::isValid("/track_20260331.csv"));
  EXPECT_FALSE(TrackFilename::isValid("/data.csv"));
}

TEST(TrackFilename, RejectsFilenameWithoutCsvExtension) {
  EXPECT_FALSE(TrackFilename::isValid("/gps_20260331_143000.txt"));
  EXPECT_FALSE(TrackFilename::isValid("/gps_20260331_143000"));
}

TEST(TrackFilename, RejectsPathTraversal) {
  EXPECT_FALSE(TrackFilename::isValid("/gps_../../etc/passwd.csv"));
  EXPECT_FALSE(TrackFilename::isValid("/gps_..data.csv"));
}

TEST(TrackFilename, RejectsFilenameWithoutLeadingSlash) {
  EXPECT_FALSE(TrackFilename::isValid("gps_20260331_143000.csv"));
}

// --- Normalization ---

TEST(TrackFilename, NormalizePreservesLeadingSlash) {
  String result = TrackFilename::normalize("/gps_20260331.csv");
  EXPECT_STREQ(result.c_str(), "/gps_20260331.csv");
}

TEST(TrackFilename, NormalizeAddsLeadingSlash) {
  String result = TrackFilename::normalize("gps_20260331.csv");
  EXPECT_STREQ(result.c_str(), "/gps_20260331.csv");
}

// --- Events sidecar names (issue #139) ---

TEST(TrackFilename, EventSidecarForTrackPath) {
  EXPECT_EQ(TrackFilename::eventSidecarFor("/gps_20250101_120000.csv"),
            String("/gps_20250101_120000.events.jsonl"));
}

TEST(TrackFilename, IsEventSidecarAcceptsSidecarNames) {
  EXPECT_TRUE(
      TrackFilename::isEventSidecar("/gps_20250101_120000.events.jsonl"));
}

TEST(TrackFilename, IsEventSidecarRejectsTracksAndForeignFiles) {
  EXPECT_FALSE(TrackFilename::isEventSidecar("/gps_20250101_120000.csv"));
  EXPECT_FALSE(TrackFilename::isEventSidecar("/log_1.txt"));
  EXPECT_FALSE(TrackFilename::isEventSidecar("/other.events.jsonl"));
  EXPECT_FALSE(TrackFilename::isEventSidecar("/gps_../x.events.jsonl"));
}

TEST(TrackFilename, SidecarOfValidTrackIsRecognised) {
  String track = "/gps_20250101_120000.csv";
  ASSERT_TRUE(TrackFilename::isValid(track));
  EXPECT_TRUE(
      TrackFilename::isEventSidecar(TrackFilename::eventSidecarFor(track)));
}
