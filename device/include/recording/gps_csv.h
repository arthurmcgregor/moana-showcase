#pragma once

#include <Arduino.h>

#include "hardware/gps_types.h"
#include "hardware/imu_types.h"

// Pure functions for GPS track CSV formatting.
// No file I/O, no heap allocation.
namespace GPSCSV {

inline const char* header() {
  return "timestamp,lat,lng,altitude,speed,course,satellites,hdop,gps_date,gps_time,heel,trim,"
         "heading";
}

// Format a CSV row into buf. Returns number of bytes written (excluding null).
// IMU columns are written as floats when imu.valid, or empty cells otherwise
// (matches pre-IMU firmware so the parser only needs one path).
size_t formatRow(char* buf, size_t bufSize, unsigned long timestamp, const GPSData& gps,
                 const IMUSnapshot& imu);

String generateFilename();

}  // namespace GPSCSV
