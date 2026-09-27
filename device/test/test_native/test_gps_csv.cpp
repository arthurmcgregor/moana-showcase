#include <gtest/gtest.h>

#include <cstring>
#include <regex>
#include <string>

#include "recording/gps_csv.h"

namespace {
GPSData makeGPS() {
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

constexpr IMUSnapshot kNoIMU{0.0f, 0.0f, 0.0f, false};
constexpr IMUSnapshot kSampleIMU{12.34f, -2.50f, 274.80f, true};
}  // namespace

TEST(GPSCSV, HeaderMatchesExpectedColumns) {
  std::string header = GPSCSV::header();
  EXPECT_EQ(header,
            "timestamp,lat,lng,altitude,speed,course,satellites,hdop,gps_date,gps_time,heel,trim,"
            "heading");
}

TEST(GPSCSV, FormatRowProducesCorrectCSVWithIMU) {
  GPSData data = makeGPS();

  char buf[256];
  size_t len = GPSCSV::formatRow(buf, sizeof(buf), 1000, data, kSampleIMU);

  EXPECT_GT(len, 0u);
  std::string row(buf);
  EXPECT_EQ(row,
            "1000,-36.84846688,174.76333045,42.50,5.25,180.75,12,0.90,310326,14300000,12.34,-2.50,"
            "274.80");
}

TEST(GPSCSV, FormatRowEmitsEmptyIMUColumnsWhenInvalid) {
  GPSData data = makeGPS();

  char buf[256];
  size_t len = GPSCSV::formatRow(buf, sizeof(buf), 1000, data, kNoIMU);

  EXPECT_GT(len, 0u);
  std::string row(buf);
  EXPECT_EQ(row, "1000,-36.84846688,174.76333045,42.50,5.25,180.75,12,0.90,310326,14300000,,,");
}

TEST(GPSCSV, FormatRowPreserves8DecimalPrecisionForCoordinates) {
  GPSData data{};
  data.lat = -36.12345678;
  data.lng = 174.87654321;

  char buf[256];
  GPSCSV::formatRow(buf, sizeof(buf), 0, data, kNoIMU);

  std::string row(buf);
  EXPECT_NE(row.find("-36.12345678"), std::string::npos);
  EXPECT_NE(row.find("174.87654321"), std::string::npos);
}

TEST(GPSCSV, FormatRowFailsGracefullyWhenBufferTooSmall) {
  GPSData data = makeGPS();

  char buf[10];
  size_t len = GPSCSV::formatRow(buf, sizeof(buf), 1000, data, kNoIMU);
  EXPECT_EQ(len, 0u);
}

TEST(GPSCSV, GenerateFilenameMatchesExpectedPattern) {
  String filename = GPSCSV::generateFilename();
  std::string name(filename.c_str());

  std::regex pattern(R"(/gps_\d{8}_\d{6}\.csv)");
  EXPECT_TRUE(std::regex_match(name, pattern))
      << "Filename '" << name << "' does not match /gps_YYYYMMDD_HHMMSS.csv pattern";
}
