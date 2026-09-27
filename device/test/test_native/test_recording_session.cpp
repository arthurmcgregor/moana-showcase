#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "fake_filesystem.h"
#include "recording/gps_csv.h"
#include "recording/recorded_sample.h"
#include "recording/recording_session.h"

namespace {

const char* kPath = "/gps_20260101_120000.csv";

RecordedSample makeSample(uint32_t capturedAtMs, double lat, double lng) {
  RecordedSample s{};
  s.gps.lat = lat;
  s.gps.lng = lng;
  s.imu.valid = false;
  s.capturedAtMs = capturedAtMs;
  return s;
}

std::vector<std::string> lines(const std::string& content) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : content) {
    if (c == '\n') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

}  // namespace

class RecordingSessionTest : public ::testing::Test {
 protected:
  FakeFileSystem fs;
  RecordingSession makeSession() { return RecordingSession(fs); }
};

// --- begin() ---

TEST_F(RecordingSessionTest, BeginWritesHeaderAndReportsOpen) {
  auto session = makeSession();
  ASSERT_TRUE(session.begin(kPath));
  EXPECT_TRUE(session.isOpen());

  auto rows = lines(fs.readFileContent(kPath));
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0], std::string(GPSCSV::header()));
}

TEST_F(RecordingSessionTest, BeginFailsWhenFileCannotOpen) {
  fs.setOpenWriteSucceeds(false);
  auto session = makeSession();
  EXPECT_FALSE(session.begin(kPath));
  EXPECT_FALSE(session.isOpen());
}

TEST_F(RecordingSessionTest, BeginFailsWhenHeaderWriteFails) {
  fs.setWriteSucceeds(false);
  auto session = makeSession();
  EXPECT_FALSE(session.begin(kPath));
  // Session must be left closed so no further writes are attempted.
  EXPECT_FALSE(session.isOpen());
}

// --- B3: capture-time timestamps ---

TEST_F(RecordingSessionTest, EachRowUsesItsOwnCaptureTimestamp) {
  auto session = makeSession();
  ASSERT_TRUE(session.begin(kPath));

  RecordedSample batch[] = {
      makeSample(1000, -36.80, 174.70),
      makeSample(2000, -36.81, 174.71),
      makeSample(3000, -36.82, 174.72),
  };
  ASSERT_TRUE(session.writeBatch(batch, 3));

  auto rows = lines(fs.readFileContent(kPath));
  ASSERT_EQ(rows.size(), 4u);  // header + 3 rows
  // Each row's timestamp column (first field) is that sample's capture time,
  // NOT a single shared flush-time value.
  EXPECT_EQ(rows[1].rfind("1000,", 0), 0u);
  EXPECT_EQ(rows[2].rfind("2000,", 0), 0u);
  EXPECT_EQ(rows[3].rfind("3000,", 0), 0u);
}

// --- append semantics ---

TEST_F(RecordingSessionTest, BatchesAppendRatherThanOverwrite) {
  auto session = makeSession();
  ASSERT_TRUE(session.begin(kPath));

  RecordedSample first[] = {makeSample(1000, -36.80, 174.70)};
  RecordedSample second[] = {makeSample(2000, -36.81, 174.71)};
  ASSERT_TRUE(session.writeBatch(first, 1));
  ASSERT_TRUE(session.writeBatch(second, 1));

  auto rows = lines(fs.readFileContent(kPath));
  ASSERT_EQ(rows.size(), 3u);  // header + 2 rows
  EXPECT_EQ(rows[1].rfind("1000,", 0), 0u);
  EXPECT_EQ(rows[2].rfind("2000,", 0), 0u);
}

TEST_F(RecordingSessionTest, WriteBatchWritesExactlyTheGivenRows) {
  // Guards B2b: the session only ever writes samples it is handed — it can
  // never emit a phantom "null island" (0,0) row.
  auto session = makeSession();
  ASSERT_TRUE(session.begin(kPath));

  RecordedSample batch[] = {makeSample(1000, -36.80, 174.70),
                            makeSample(2000, -36.81, 174.71)};
  ASSERT_TRUE(session.writeBatch(batch, 2));

  auto rows = lines(fs.readFileContent(kPath));
  EXPECT_EQ(rows.size(), 3u);  // header + exactly 2 rows, nothing extra
}

// --- B1: write / flush failure detection ---

TEST_F(RecordingSessionTest, WriteBatchReturnsFalseWhenWriteFails) {
  auto session = makeSession();
  ASSERT_TRUE(session.begin(kPath));

  fs.setWriteSucceeds(false);
  RecordedSample batch[] = {makeSample(1000, -36.80, 174.70)};
  EXPECT_FALSE(session.writeBatch(batch, 1));
}

TEST_F(RecordingSessionTest, WriteBatchReturnsFalseWhenFlushFails) {
  auto session = makeSession();
  ASSERT_TRUE(session.begin(kPath));

  fs.setFlushSucceeds(false);
  RecordedSample batch[] = {makeSample(1000, -36.80, 174.70)};
  EXPECT_FALSE(session.writeBatch(batch, 1));
}

TEST_F(RecordingSessionTest, EarlierRowsSurviveALaterWriteFailure) {
  // A first successful batch stays on disk even if a later batch fails —
  // matching the recorder's "abort but keep what was written" behaviour.
  auto session = makeSession();
  ASSERT_TRUE(session.begin(kPath));

  RecordedSample good[] = {makeSample(1000, -36.80, 174.70)};
  ASSERT_TRUE(session.writeBatch(good, 1));

  fs.setWriteSucceeds(false);
  RecordedSample bad[] = {makeSample(2000, -36.81, 174.71)};
  EXPECT_FALSE(session.writeBatch(bad, 1));

  auto rows = lines(fs.readFileContent(kPath));
  ASSERT_EQ(rows.size(), 2u);  // header + the one good row
  EXPECT_EQ(rows[1].rfind("1000,", 0), 0u);
}

// --- guards ---

TEST_F(RecordingSessionTest, WriteBatchBeforeBeginReturnsFalse) {
  auto session = makeSession();
  RecordedSample batch[] = {makeSample(1000, -36.80, 174.70)};
  EXPECT_FALSE(session.writeBatch(batch, 1));
}

TEST_F(RecordingSessionTest, WriteBatchAfterEndReturnsFalse) {
  auto session = makeSession();
  ASSERT_TRUE(session.begin(kPath));
  session.end();
  EXPECT_FALSE(session.isOpen());

  RecordedSample batch[] = {makeSample(1000, -36.80, 174.70)};
  EXPECT_FALSE(session.writeBatch(batch, 1));
}

TEST_F(RecordingSessionTest, EmptyBatchIsANoopThatSucceeds) {
  auto session = makeSession();
  ASSERT_TRUE(session.begin(kPath));
  EXPECT_TRUE(session.writeBatch(nullptr, 0));

  auto rows = lines(fs.readFileContent(kPath));
  EXPECT_EQ(rows.size(), 1u);  // header only
}
