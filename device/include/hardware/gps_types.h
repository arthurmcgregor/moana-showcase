#pragma once

// Pure data types for GPS — no hardware dependencies.
// Extracted so modules that only need GPSData don't pull in TinyGPSPlus.

struct GPSData {
  double altitude;         // Altitude above mean sea level in meters
  double course;           // Course over ground in degrees
  double hdop;             // Horizontal dilution of precision
  double lat;              // Latitude in decimal degrees
  double lng;              // Longitude in decimal degrees
  double speed;            // Speed over ground in knots
  int satellites;          // Number of satellites used for fix
  unsigned long gps_date;  // UTC date from GPS (DDMMYY)
  unsigned long gps_time;  // UTC time from GPS (HHMMSSCC)
};
