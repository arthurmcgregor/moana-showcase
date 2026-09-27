#include "recording/gps_csv.h"

#include <ctime>

size_t GPSCSV::formatRow(char* buf, size_t bufSize, unsigned long timestamp, const GPSData& gps,
                         const IMUSnapshot& imu) {
  int len;
  if (imu.valid) {
    len = snprintf(buf, bufSize, "%lu,%.8f,%.8f,%.2f,%.2f,%.2f,%d,%.2f,%lu,%lu,%.2f,%.2f,%.2f",
                   timestamp, gps.lat, gps.lng, gps.altitude, gps.speed, gps.course, gps.satellites,
                   gps.hdop, gps.gps_date, gps.gps_time, imu.heel, imu.trim, imu.heading);
  } else {
    len = snprintf(buf, bufSize, "%lu,%.8f,%.8f,%.2f,%.2f,%.2f,%d,%.2f,%lu,%lu,,,", timestamp,
                   gps.lat, gps.lng, gps.altitude, gps.speed, gps.course, gps.satellites, gps.hdop,
                   gps.gps_date, gps.gps_time);
  }
  return (len > 0 && (size_t)len < bufSize) ? (size_t)len : 0;
}

String GPSCSV::generateFilename() {
  struct tm timeinfo;
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);

  char buf[80];  // sized for GCC's worst-case %d estimate, not just the ~24-char name
  snprintf(buf, sizeof(buf), "/gps_%04d%02d%02d_%02d%02d%02d.csv", timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
           timeinfo.tm_sec);

  return String(buf);
}
