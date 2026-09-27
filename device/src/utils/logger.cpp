#include "utils/logger.h"

#include <stdarg.h>
#include <time.h>

#include <LittleFS.h>
#include <M5Unified.h>

static const char* LOG_TAG = "Logger";

static esp_log_level_t toEspLevel(LogLevel level) {
  switch (level) {
    case LogLevel::DEBUG: return ESP_LOG_DEBUG;
    case LogLevel::INFO:  return ESP_LOG_INFO;
    case LogLevel::WARN:  return ESP_LOG_WARN;
    case LogLevel::ERROR: return ESP_LOG_ERROR;
    default:              return ESP_LOG_INFO;
  }
}

static const char* levelToString(LogLevel level) {
  switch (level) {
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO:  return "INFO ";
    case LogLevel::WARN:  return "WARN ";
    case LogLevel::ERROR: return "ERROR";
    default:              return "?????";
  }
}

Logger& Logger::getInstance() {
  static Logger instance;
  return instance;
}

bool Logger::init() {
  if (initialized_.load()) {
    return true;
  }

  pathMutex_ = xSemaphoreCreateMutex();
  rotateDone_ = xSemaphoreCreateBinary();
  logQueue_ = xQueueCreate(QUEUE_DEPTH, sizeof(QueuedLog));
  if (!pathMutex_ || !rotateDone_ || !logQueue_) {
    return false;
  }

  currentLogFile_ = generateLogFileName();
  currentSize_ = 0;
  if (!writeHeaderIfNew()) {
    return false;  // dead filesystem — let app.cpp report it instead of silently proceeding
  }

  // Low priority: the writer must never preempt real work. It drains the queue
  // and does the (blocking) flash I/O off the callers' tasks.
  if (xTaskCreate(writerTask, "LogWriter", 4096, this, 1, &writerHandle_) != pdPASS) {
    return false;
  }

  initialized_.store(true);
  LOG_INFO(LOG_TAG, "Initialized async logging to %s", currentLogFile_.c_str());
  return true;
}

// ============================================
// Hot path (runs on the caller's task)
// ============================================

void Logger::enqueue(LogLevel level, const char* tag, const char* body) {
  if (serialLoggingEnabled_.load()) {
    M5.Log(toEspLevel(level), "[%s] %s", tag, body);
  }

  if (!logQueue_) {
    return;  // pre-init: serial only, no queue yet
  }

  QueuedLog msg;
  msg.timestampMs = millis();
  msg.level = level;
  snprintf(msg.tag, sizeof(msg.tag), "%s", tag);
  snprintf(msg.body, sizeof(msg.body), "%s", body);

  // Never block a caller on the log path: drop (and count) if the writer is
  // behind. xQueueSend copies the record by value.
  if (xQueueSend(logQueue_, &msg, 0) != pdTRUE) {
    droppedCount_.fetch_add(1);
  }
}

void Logger::vlog(LogLevel level, const char* tag, const char* format, va_list args) {
  char buffer[256];
  vsnprintf(buffer, sizeof(buffer), format, args);
  enqueue(level, tag, buffer);
}

void Logger::log(LogLevel level, const char* tag, const char* format, ...) {
  if (level < minLogLevel_.load()) return;
  va_list args;
  va_start(args, format);
  vlog(level, tag, format, args);
  va_end(args);
}

void Logger::debug(const char* tag, const char* format, ...) {
  if (LogLevel::DEBUG < minLogLevel_.load()) return;
  va_list args;
  va_start(args, format);
  vlog(LogLevel::DEBUG, tag, format, args);
  va_end(args);
}

void Logger::info(const char* tag, const char* format, ...) {
  if (LogLevel::INFO < minLogLevel_.load()) return;
  va_list args;
  va_start(args, format);
  vlog(LogLevel::INFO, tag, format, args);
  va_end(args);
}

void Logger::warn(const char* tag, const char* format, ...) {
  if (LogLevel::WARN < minLogLevel_.load()) return;
  va_list args;
  va_start(args, format);
  vlog(LogLevel::WARN, tag, format, args);
  va_end(args);
}

void Logger::error(const char* tag, const char* format, ...) {
  if (LogLevel::ERROR < minLogLevel_.load()) return;
  va_list args;
  va_start(args, format);
  vlog(LogLevel::ERROR, tag, format, args);
  va_end(args);
}

// ============================================
// Writer task (owns the LittleFS file)
// ============================================

void Logger::writerTask(void* param) {
  Logger* self = static_cast<Logger*>(param);
  QueuedLog first;
  for (;;) {
    // Block up to one poll interval for the next record; the timeout also lets
    // a pending rotate request be serviced promptly when the queue is idle.
    bool got = xQueueReceive(self->logQueue_, &first, pdMS_TO_TICKS(WRITER_POLL_MS)) == pdTRUE;

    if (got) {
      File f = LittleFS.open(self->currentLogFile_.c_str(), "a");
      if (f) {
        char line[288];
        size_t n = formatLine(line, sizeof(line), first);
        f.write(reinterpret_cast<const uint8_t*>(line), n);
        self->currentSize_ += n;
        // Drain whatever else is already queued into the same open file.
        QueuedLog m;
        while (xQueueReceive(self->logQueue_, &m, 0) == pdTRUE) {
          n = formatLine(line, sizeof(line), m);
          f.write(reinterpret_cast<const uint8_t*>(line), n);
          self->currentSize_ += n;
        }
        // Record any records the hot path had to drop (queue full) so the loss
        // is visible in the log rather than silent (R7).
        size_t dn = self->buildDroppedNotice(line, sizeof(line));
        if (dn) {
          f.write(reinterpret_cast<const uint8_t*>(line), dn);
          self->currentSize_ += dn;
        }
        f.close();
      }
    }

    if (self->rotateRequested_.exchange(false)) {
      // Explicit rotation (LOG_ROTATE): drain stragglers to the old file, then
      // start fresh, and signal the caller waiting in rotateLogs().
      self->drainToFile();
      self->rotateFile();
      xSemaphoreGive(self->rotateDone_);
    } else if (self->currentSize_ >= MAX_LOG_FILE_SIZE) {
      self->rotateFile();
    }
  }
}

void Logger::drainToFile() {
  File f = LittleFS.open(currentLogFile_.c_str(), "a");
  if (!f) return;
  char line[288];
  QueuedLog m;
  while (xQueueReceive(logQueue_, &m, 0) == pdTRUE) {
    size_t n = formatLine(line, sizeof(line), m);
    f.write(reinterpret_cast<const uint8_t*>(line), n);
    currentSize_ += n;
  }
  size_t dn = buildDroppedNotice(line, sizeof(line));
  if (dn) {
    f.write(reinterpret_cast<const uint8_t*>(line), dn);
    currentSize_ += dn;
  }
  f.close();
}

bool Logger::writeHeaderIfNew() {
  File f = LittleFS.open(currentLogFile_.c_str(), "a");
  if (!f) return false;

  if (f.size() == 0) {
    f.println("========================================");
    f.println("     Moana GPS Tracker - Log File      ");
    f.println("========================================");

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year > (2020 - 1900)) {
      char timeStr[64];
      strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
      f.printf("Start Time: %s\n", timeStr);
    } else {
      f.printf("Start Time: %lu ms since boot\n", (unsigned long)millis());
    }
    f.println("========================================\n");
  }

  currentSize_ = f.size();
  f.close();
  return true;
}

void Logger::rotateFile() {
  String name = generateLogFileName();
  if (pathMutex_) xSemaphoreTake(pathMutex_, portMAX_DELAY);
  currentLogFile_ = name;
  if (pathMutex_) xSemaphoreGive(pathMutex_);
  currentSize_ = 0;
  writeHeaderIfNew();
}

String Logger::generateLogFileName() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  char filename[64];
  if (timeinfo.tm_year > (2020 - 1900)) {
    snprintf(filename, sizeof(filename), "%s%04d%02d%02d_%02d%02d%02d%s", LOG_PREFIX,
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour,
             timeinfo.tm_min, timeinfo.tm_sec, LOG_EXTENSION);
  } else {
    snprintf(filename, sizeof(filename), "%s%010lu%s", LOG_PREFIX, (unsigned long)millis(),
             LOG_EXTENSION);
  }
  return String(filename);
}

size_t Logger::formatLine(char* out, size_t outSize, const QueuedLog& msg) {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  int n;
  if (timeinfo.tm_year > (2020 - 1900)) {
    n = snprintf(out, outSize, "[%04d-%02d-%02d %02d:%02d:%02d.%03lu] [%s] [%s] %s\n",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour,
                 timeinfo.tm_min, timeinfo.tm_sec, (unsigned long)(msg.timestampMs % 1000),
                 levelToString(msg.level), msg.tag, msg.body);
  } else {
    n = snprintf(out, outSize, "[%10lu] [%s] [%s] %s\n", (unsigned long)msg.timestampMs,
                 levelToString(msg.level), msg.tag, msg.body);
  }
  if (n < 0) return 0;
  return (static_cast<size_t>(n) < outSize) ? static_cast<size_t>(n) : (outSize - 1);
}

size_t Logger::buildDroppedNotice(char* out, size_t outSize) {
  uint32_t dropped = droppedCount_.exchange(0);
  if (dropped == 0) return 0;
  QueuedLog notice;
  notice.timestampMs = millis();
  notice.level = LogLevel::WARN;
  snprintf(notice.tag, sizeof(notice.tag), "Logger");
  snprintf(notice.body, sizeof(notice.body), "%lu log message(s) dropped (queue full)",
           (unsigned long)dropped);
  return formatLine(out, outSize, notice);
}

// ============================================
// Public control (called from other tasks)
// ============================================

bool Logger::rotateLogs() {
  if (!logQueue_ || !rotateDone_) return false;
  xSemaphoreTake(rotateDone_, 0);  // clear any stale completion
  rotateRequested_.store(true);
  // The writer services the flag within one poll interval, then signals.
  return xSemaphoreTake(rotateDone_, pdMS_TO_TICKS(ROTATE_TIMEOUT_MS)) == pdTRUE;
}

String Logger::getCurrentLogFilePath() {
  if (!pathMutex_) return currentLogFile_;
  xSemaphoreTake(pathMutex_, portMAX_DELAY);
  String path = currentLogFile_;
  xSemaphoreGive(pathMutex_);
  return path;
}
