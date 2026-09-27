#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <unity.h>

#include "recording/gps_csv.h"
#include "storage/storage.h"

static Storage storage;

static const char* TEST_FILENAME = "/gps_00000000_000000.csv";

static GPSData makeTestData() {
  GPSData d{};
  d.lat = -36.84846688;
  d.lng = 174.76333045;
  d.altitude = 42.50;
  d.speed = 5.25;
  d.course = 180.75;
  d.satellites = 12;
  d.hdop = 0.90;
  d.gps_date = 310326;
  d.gps_time = 14300000;
  return d;
}

// --- Mount ---

void test_littlefs_mounts() {
  TEST_ASSERT_TRUE_MESSAGE(LittleFS.begin(false), "LittleFS failed to mount");
}

// --- Clean up any leftover test file from a previous run ---

void test_cleanup_leftover() {
  LittleFS.remove(TEST_FILENAME);
  // No assertion — file may or may not exist
}

// --- Write a track file ---

void test_write_track_file() {
  File file = LittleFS.open(TEST_FILENAME, "w");
  TEST_ASSERT_TRUE_MESSAGE(file, "Failed to open file for writing");

  file.println(GPSCSV::header());
  file.flush();

  GPSData data = makeTestData();
  IMUSnapshot imu{};  // valid == false → empty heel/trim/heading cells
  char rowBuf[160];
  size_t len = GPSCSV::formatRow(rowBuf, sizeof(rowBuf), 1000, data, imu);
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, len, "formatRow returned 0");

  file.write((const uint8_t*)rowBuf, len);
  file.write('\n');
  file.flush();
  file.close();
}

// --- Verify via Storage listing ---

void test_track_appears_in_listing() {
  auto tracks = storage.localTracks();

  bool found = false;
  for (const auto& t : tracks) {
    if (t == TEST_FILENAME) {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Test track not found in listing");
}

// --- Read back and verify content ---

void test_track_content_matches() {
  File file = LittleFS.open(TEST_FILENAME, "r");
  TEST_ASSERT_TRUE_MESSAGE(file, "Failed to open file for reading");

  // Verify header
  String header = file.readStringUntil('\n');
  header.trim();
  TEST_ASSERT_EQUAL_STRING(GPSCSV::header(), header.c_str());

  // Verify data row
  String row = file.readStringUntil('\n');
  row.trim();
  // Trailing ",,," are the empty heel/trim/heading columns (IMU invalid).
  TEST_ASSERT_EQUAL_STRING(
      "1000,-36.84846688,174.76333045,42.50,5.25,180.75,12,0.90,310326,14300000,,,", row.c_str());

  file.close();
}

// --- Delete and verify cleanup ---

void test_delete_track() {
  TEST_ASSERT_TRUE_MESSAGE(LittleFS.remove(TEST_FILENAME), "Failed to delete track file");
}

void test_track_gone_after_delete() {
  auto tracks = storage.localTracks();

  for (const auto& t : tracks) {
    TEST_ASSERT_FALSE_MESSAGE(t == TEST_FILENAME, "Test track still present after delete");
  }
}

// --- Entry point ---

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  delay(2000);
  UNITY_BEGIN();

  RUN_TEST(test_littlefs_mounts);
  RUN_TEST(test_cleanup_leftover);
  RUN_TEST(test_write_track_file);
  RUN_TEST(test_track_appears_in_listing);
  RUN_TEST(test_track_content_matches);
  RUN_TEST(test_delete_track);
  RUN_TEST(test_track_gone_after_delete);

  UNITY_END();
}

void loop() {
  M5.update();
  delay(100);
}
