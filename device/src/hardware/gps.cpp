#include "hardware/gps.h"

#include "app.h"
#include "hardware/gps_policy.h"
#include "utils/logger.h"

static const char* LOG_TAG = "GPS";

GPS::GPS() : serial(2), uart_num(2), rx_pin(18), tx_pin(17), baud(115200) {}

void GPS::init(App* app) {
  LOG_INFO(LOG_TAG, "Initializing GPS...");
  // Enlarge the UART RX buffer from the 256 B default: at 115200 baud ~1.15 KB
  // of NMEA can arrive between the 100 ms drains, so a busy sentence mix (nav +
  // GSV) would overflow the default and corrupt checksums, flapping the fix
  // LOCKED->LOST (R6).
  serial.setRxBufferSize(1024);
  serial.begin(baud, SERIAL_8N1, rx_pin, tx_pin);
  app->postEvent(EventType::GPS_STATE, GPSState::ACQUIRING);
}

void GPS::dataTask(void* pvParameters) {
  LOG_DEBUG(LOG_TAG, "Starting gps data task");
  App* app = static_cast<App*>(pvParameters);
  GPS* gps = &app->getGPS();

  while (true) {
    gps->read();
    GPSData data;

    // GPS task just reports facts: valid data when available
    if (gps->getLatestData(data)) {
      app->postEvent(EventType::GPS_DATA, data);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void GPS::healthTask(void* pvParameters) {
  LOG_INFO(LOG_TAG, "GPS health task started");
  App* app = static_cast<App*>(pvParameters);

  while (true) {
    GPSState current = app->getGPSState();
    GPSState next =
        GpsPolicy::healthCheck(current, app->getGPS().fixAgeMs(), FIX_STALE_MS);
    if (next != current) {
      LOG_WARN(LOG_TAG, "GPS fix stale");
      app->postEvent(EventType::GPS_STATE, next);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));  // Check every second
  }
}

void GPS::read() {
  while (serial.available() > 0) {
    gps.encode(serial.read());
  }
}

bool GPS::getLatestData(GPSData& data) {
  if (gps.location.isUpdated() && gps.location.isValid()) {
    // LOG_DEBUG(LOG_TAG, "Latest data acquired");
    data.lat = gps.location.lat();
    data.lng = gps.location.lng();
    data.speed = gps.speed.knots();
    data.satellites = gps.satellites.value();
    data.altitude = gps.altitude.meters();
    data.hdop = gps.hdop.value() / 100.0;
    data.course = gps.course.deg();
    data.gps_date = gps.date.value();
    data.gps_time = gps.time.value();
    return true;
  }
  return false;
}

unsigned long GPS::fixAgeMs() { return gps.location.age(); }
