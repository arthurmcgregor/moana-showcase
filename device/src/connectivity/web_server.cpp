#include "connectivity/web_server.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <memory>

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <M5Unified.h>

#include "storage/storage.h"
#include "utils/logger.h"

static const char* LOG_TAG = "WebServer";

WebServer::WebServer() : server(80) {
  gpsQueue = xQueueCreate(1, sizeof(GPSData));
}

void WebServer::init(Storage* storage) {
  this->storage = storage;
}

void WebServer::begin() {
  setupRoutes();
  server.begin();
  lastRequestTime.store(millis());
  LOG_INFO(LOG_TAG, "Webserver started");
}

void WebServer::recordRequestTime() {
  lastRequestTime.store(millis());
}

void WebServer::setupRoutes() {
  // Enable CORS for all routes
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                       "GET, POST, PUT, DELETE, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  server.on("/gps", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGPS(request); });

  server.on("/tracks/list", HTTP_GET,
            [this](AsyncWebServerRequest* request) { handleTracksList(request); });

  // Use query parameter for track downloads: /track?file=filename.csv
  server.on("/track", HTTP_GET,
            [this](AsyncWebServerRequest* request) { handleTrackFile(request); });

  // Delete a track: /track?file=filename.csv
  server.on("/track", HTTP_DELETE,
            [this](AsyncWebServerRequest* request) { handleDeleteTrack(request); });

  server.serveStatic("/", LittleFS, "/frontend").setDefaultFile("index.html");
}

void WebServer::handleGPS(AsyncWebServerRequest* request) {
  recordRequestTime();
  GPSData data;
  LOG_INFO(LOG_TAG, "handleGPS: Request received from %s",
           request->client()->remoteIP().toString().c_str());

  // The queue holds only the latest fix (xQueueOverwrite), so peek without
  // blocking — the old 2 s wait pinned the whole async HTTP server whenever
  // there was no fix (R2). No fix yet, or a stale one (>5 s old), is a 503.
  uint32_t lastFix = lastGpsMs.load();
  bool haveFix = xQueuePeek(gpsQueue, &data, 0) == pdTRUE;
  if (haveFix && lastFix != 0 && (millis() - lastFix) <= GPS_STALE_MS) {
    uint32_t jsonStart = millis();
    JsonDocument doc;
    doc["lat"] = data.lat;
    doc["lng"] = data.lng;
    doc["speed"] = data.speed;
    doc["satellites"] = data.satellites;
    doc["altitude"] = data.altitude;
    doc["hdop"] = data.hdop;
    doc["course"] = data.course;
    String json;
    serializeJson(doc, json);
    uint32_t jsonElapsed = millis() - jsonStart;
    LOG_INFO(LOG_TAG, "handleGPS: JSON serialization took %lu ms", (unsigned long)jsonElapsed);
    request->send(200, "application/json", json);
  } else {
    LOG_WARN(LOG_TAG, "handleGPS: no fresh GPS fix (have=%d, age=%lu ms)", haveFix,
             lastFix ? (unsigned long)(millis() - lastFix) : 0UL);
    request->send(503, "application/json", "{\"error\":\"No GPS fix\"}");
  }
}

void WebServer::receiveGPSData(const GPSData& data) {
  if (gpsQueue) {
    xQueueOverwrite(gpsQueue, &data);
    lastGpsMs.store(millis());
  }
}

void WebServer::handleTracksList(AsyncWebServerRequest* request) {
  recordRequestTime();
  // Storage rejects archive access during a sync (B4); surface that as a 503
  // so the client retries rather than interpreting an empty body as "no tracks"
  // and clearing its list.
  if (storage->isSyncing()) {
    LOG_INFO(LOG_TAG, "handleTracksList: sync in progress, returning 503");
    request->send(503, "application/json", "{\"error\":\"Sync in progress, retry shortly\"}");
    return;
  }
  uint32_t startTime = millis();
  LOG_INFO(LOG_TAG, "handleTracksList: Request received from %s",
           request->client()->remoteIP().toString().c_str());

  LOG_INFO(LOG_TAG, "handleTracksList: Calling archivedTracks()");
  ArchiveListing listing = storage->archivedTracks();

  // Don't let a mount failure or a busy archive masquerade as an empty 200 —
  // the client can't tell that apart from "no tracks" and would clear its list.
  if (listing.status == ArchiveStatus::MountFailed) {
    LOG_ERROR(LOG_TAG, "handleTracksList: SD mount failed, returning 500");
    request->send(500, "application/json",
                  "{\"error\":\"SD card unavailable (missing or failed)\"}");
    return;
  }
  if (listing.status == ArchiveStatus::Busy) {
    LOG_INFO(LOG_TAG, "handleTracksList: archive busy, returning 503");
    request->send(503, "application/json", "{\"error\":\"Archive busy, retry shortly\"}");
    return;
  }
  LOG_INFO(LOG_TAG, "handleTracksList: Found %d tracks", listing.tracks.size());

  JsonDocument doc;
  JsonArray arr = doc["tracks"].to<JsonArray>();
  for (const auto& track : listing.tracks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = track.name;
    obj["size"] = track.size;
  }
  String json;
  serializeJson(doc, json);

  uint32_t elapsed = millis() - startTime;
  LOG_INFO(LOG_TAG, "handleTracksList: Response sent (%d bytes) in %lu ms", json.length(),
           (unsigned long)elapsed);
  request->send(200, "application/json", json);
}

bool WebServer::extractFileParam(AsyncWebServerRequest* request, String& filename) {
  if (!request->hasParam("file")) {
    request->send(400, "application/json", "{\"error\":\"No file parameter specified\"}");
    return false;
  }

  filename = request->getParam("file")->value();
  if (filename.isEmpty()) {
    request->send(400, "application/json", "{\"error\":\"Empty filename\"}");
    return false;
  }

  return true;
}

void WebServer::handleTrackFile(AsyncWebServerRequest* request) {
  recordRequestTime();
  if (storage->isSyncing()) {
    LOG_INFO(LOG_TAG, "handleTrackFile: sync in progress, returning 503");
    request->send(503, "application/json", "{\"error\":\"Sync in progress, retry shortly\"}");
    return;
  }
  uint32_t startTime = millis();
  LOG_INFO(LOG_TAG, "handleTrackFile: Request received from %s for URL: %s",
           request->client()->remoteIP().toString().c_str(), request->url().c_str());

  String filename;
  if (!extractFileParam(request, filename)) return;

  LOG_INFO(LOG_TAG, "handleTrackFile: Requesting file: %s", filename.c_str());

  // Move the TrackFile into a shared_ptr so the chunked-response filler can own
  // it for the whole response — it keeps the SD mount + archive claim alive and
  // releases them when the response completes and the filler is destroyed.
  auto trackFile = std::make_shared<TrackFile>(storage->openArchived(filename));

  if (!*trackFile) {
    LOG_ERROR(LOG_TAG, "handleTrackFile: Failed to open file: %s", filename.c_str());
    request->send(404, "application/json", "{\"error\":\"Track not found\"}");
    return;
  }

  size_t fileSize = trackFile->size();
  LOG_INFO(LOG_TAG, "handleTrackFile: Streaming file (%d bytes)", fileSize);

  // Pull-based streaming: AsyncWebServer calls the filler for successive,
  // non-overlapping ranges, so a forward-only readChunk matches. Streaming
  // avoids buffering the whole track twice in the heap (R2) — a 1 h track was
  // ~640 KB transient, and unchecked reserve/concat could serve a truncated CSV
  // with HTTP 200.
  //
  // Fixed-length (not chunked) so Content-Length == fileSize: if a read errors
  // mid-stream and the filler returns short, the body violates Content-Length
  // and the client sees a failed download instead of a valid-looking truncated
  // CSV — and download progress is visible for free.
  // The same endpoint serves track CSVs and their JSONL event sidecars (#148).
  const char* contentType = filename.endsWith(".jsonl") ? "application/x-ndjson" : "text/csv";
  AsyncWebServerResponse* response = request->beginResponse(
      contentType, fileSize, [trackFile](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
        (void)index;  // forward-only; TrackFile tracks its own read position
        return trackFile->readChunk(buffer, maxLen);
      });
  response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  request->send(response);

  uint32_t elapsed = millis() - startTime;
  LOG_INFO(LOG_TAG, "handleTrackFile: Chunked response initiated (%d bytes) in %lu ms", fileSize,
           (unsigned long)elapsed);
}

void WebServer::handleDeleteTrack(AsyncWebServerRequest* request) {
  recordRequestTime();
  if (storage->isSyncing()) {
    LOG_INFO(LOG_TAG, "handleDeleteTrack: sync in progress, returning 503");
    request->send(503, "application/json", "{\"error\":\"Sync in progress, retry shortly\"}");
    return;
  }
  uint32_t startTime = millis();
  LOG_INFO(LOG_TAG, "handleDeleteTrack: Request received from %s for URL: %s",
           request->client()->remoteIP().toString().c_str(), request->url().c_str());

  String filename;
  if (!extractFileParam(request, filename)) return;

  LOG_INFO(LOG_TAG, "handleDeleteTrack: Deleting file: %s", filename.c_str());

  bool success = storage->deleteArchived(filename);

  uint32_t elapsed = millis() - startTime;

  if (success) {
    LOG_INFO(LOG_TAG, "handleDeleteTrack: Successfully deleted %s in %lu ms", filename.c_str(),
             (unsigned long)elapsed);
    request->send(200, "application/json", "{\"success\":true}");
  } else {
    LOG_ERROR(LOG_TAG, "handleDeleteTrack: Failed to delete %s after %lu ms", filename.c_str(),
              (unsigned long)elapsed);
    request->send(500, "application/json", "{\"error\":\"Failed to delete track\"}");
  }
}
