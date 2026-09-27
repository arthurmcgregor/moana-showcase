#pragma once

#include <atomic>

#include <ESPAsyncWebServer.h>

#include "hardware/gps.h"

class Storage;

class WebServer {
 public:
  WebServer();

  void init(Storage* storage);
  void begin();
  void receiveGPSData(const GPSData& data);

  unsigned long getLastRequestTime() const { return lastRequestTime.load(); }

 private:
  AsyncWebServer server;
  QueueHandle_t gpsQueue;
  Storage* storage = nullptr;
  std::atomic<unsigned long> lastRequestTime{0};
  // millis() when the last GPS fix was received, for the /gps staleness check.
  std::atomic<uint32_t> lastGpsMs{0};
  static constexpr uint32_t GPS_STALE_MS = 5000;

  void recordRequestTime();
  void setupRoutes();
  void handleGPS(AsyncWebServerRequest* request);
  void handleTracksList(AsyncWebServerRequest* request);
  void handleTrackFile(AsyncWebServerRequest* request);
  void handleDeleteTrack(AsyncWebServerRequest* request);
  bool extractFileParam(AsyncWebServerRequest* request, String& filename);
};
