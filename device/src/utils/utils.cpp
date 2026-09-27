#include "utils/utils.h"

#include <M5Unified.h>

#include "app.h"
#include "utils/logger.h"

static const char* LOG_TAG = "Utils";

// NTP configuration constants - sync as UTC, apply NZ timezone only for display
#define NTP_SERVER1 "0.pool.ntp.org"
#define NTP_SERVER2 "1.pool.ntp.org"
#define NTP_SERVER3 "2.pool.ntp.org"
#define NTP_MAX_RETRIES 10

// NZ timezone for display conversion
// POSIX TZ format: NZST-12 (UTC+12), NZDT-13 (UTC+13 during DST)
// DST starts: Last Sunday of September at 2:00am
// DST ends: First Sunday of April at 3:00am
#define NZ_TIMEZONE "NZST-12NZDT-13,M9.5.0/2,M4.1.0/3"

namespace Utils {

void initTimezone() {
  // Set the display timezone once for the whole process. getLocalTime() and the
  // logger/gps_csv timestamp formatters all read this global through
  // localtime_r; the old code re-toggled TZ (UTC0 <-> NZ) inside getLocalTime()
  // on every clock refresh, which raced those readers on other tasks and
  // produced timestamps 12 h off (R10). Set it once and never touch it again.
  setenv("TZ", NZ_TIMEZONE, 1);
  tzset();
}

// Days since 1970-01-01 for a proleptic-Gregorian civil date (month in [1,12]).
// Howard Hinnant's branch-free algorithm — exact, and lets us build a UTC epoch
// without mktime/timegm (mktime consults the global TZ; timegm isn't declared
// in the ESP32 newlib headers). Keeping TZ untouched here is the whole point of
// R10: the global is set once (initTimezone) and only ever read by localtime_r.
static long long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

m5::rtc_time_t getLocalTime() {
  // Get UTC datetime from RTC
  m5::rtc_datetime_t utcDateTime = M5.Rtc.getDateTime();

  // RTC holds UTC — compute the epoch straight from the civil date so we never
  // touch the global TZ.
  long long days = daysFromCivil(utcDateTime.date.year, utcDateTime.date.month,
                                 utcDateTime.date.date);
  time_t utcEpoch = static_cast<time_t>(days * 86400LL) + utcDateTime.time.hours * 3600 +
                    utcDateTime.time.minutes * 60 + utcDateTime.time.seconds;

  // localtime_r applies the NZ timezone set once at startup (initTimezone).
  struct tm localTm;
  localtime_r(&utcEpoch, &localTm);

  // Convert back to rtc_time_t (just time fields, no date)
  m5::rtc_time_t localTime;
  localTime.hours = localTm.tm_hour;
  localTime.minutes = localTm.tm_min;
  localTime.seconds = localTm.tm_sec;

  return localTime;
}

void monitoringTask(void* pvParameters) {
  LOG_INFO(LOG_TAG, "Monitoring task started");
  App* app = static_cast<App*>(pvParameters);

  while (true) {
    LOG_DEBUG(LOG_TAG, "Monitoring task is updating UI elements");
    int batteryLevel = M5.Power.getBatteryLevel();
    m5::rtc_time_t time = getLocalTime();  // Get local NZ time for display

    app->getDisplay().updateBattery(batteryLevel);
    app->getDisplay().updateTime(time.hours, time.minutes, time.seconds);

    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

bool syncTimeWithNTP() {
  LOG_INFO(LOG_TAG, "Starting NTP time sync (UTC)...");

  // Configure NTP to sync UTC directly
  configTzTime("UTC0", NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

  struct tm timeInfo;
  int retries = 0;

  // Wait for NTP sync with timeout
  while (!getLocalTime(&timeInfo, 1000)) {
    retries++;
    LOG_DEBUG(LOG_TAG, "Waiting for NTP sync... (attempt %d/%d)", retries, NTP_MAX_RETRIES);

    if (retries >= NTP_MAX_RETRIES) {
      LOG_ERROR(LOG_TAG, "NTP sync failed after %d attempts", NTP_MAX_RETRIES);
      initTimezone();  // configTzTime left TZ=UTC0; restore NZ as the resting state
      return false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));  // Use FreeRTOS delay instead of busy-wait
  }

  LOG_INFO(LOG_TAG, "NTP sync successful");

  // Set RTC with UTC time
  time_t t = time(nullptr);
  M5.Rtc.setDateTime(gmtime(&t));  // Store as UTC

  LOG_INFO(LOG_TAG, "RTC updated to UTC: %04d-%02d-%02d %02d:%02d:%02d", timeInfo.tm_year + 1900,
           timeInfo.tm_mon + 1, timeInfo.tm_mday, timeInfo.tm_hour, timeInfo.tm_min,
           timeInfo.tm_sec);

  // configTzTime set TZ=UTC0 for the UTC sync; restore NZ as the resting state
  // so the display and log timestamps read local (R10).
  initTimezone();
  return true;
}

}  // namespace Utils
