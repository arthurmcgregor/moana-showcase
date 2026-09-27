#ifndef LOGGER_H
#define LOGGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdarg>

#include <Arduino.h>

/**
 * @brief Asynchronous logging system for M5Stack CoreS3 (F1).
 *
 * The hot path (LOG_*) never touches the filesystem or a mutex: it filters by
 * level, formats into a fixed stack buffer, optionally echoes to serial, and
 * pushes a fixed-size record onto a FreeRTOS queue (dropping, with a counter,
 * if the queue is full). A background LogWriter task drains the queue, batches
 * writes to LittleFS, and rotates the file by a size counter — so no caller
 * ever blocks on flash I/O (R7). Storage copies the rotated log files to SD
 * during its track-sync flow.
 */

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, NONE = 4 };

class Logger {
 public:
  static Logger& getInstance();

  // Create the queue + spawn the writer task. Idempotent.
  bool init();

  // Log functions (hot path — lock-free, heap-free, no FS).
  void log(LogLevel level, const char* tag, const char* format, ...);
  void debug(const char* tag, const char* format, ...);
  void info(const char* tag, const char* format, ...);
  void warn(const char* tag, const char* format, ...);
  void error(const char* tag, const char* format, ...);

  // Configuration
  void setLogLevel(LogLevel level) { minLogLevel_.store(level); }
  void setSerialLogging(bool enabled) { serialLoggingEnabled_.store(enabled); }

  // Force rotation to a fresh log file, draining anything still queued to the
  // old one first. Blocks until the writer task confirms — Storage calls this
  // (LOG_ROTATE) before copying logs to SD, so the copy sees a settled file.
  bool rotateLogs();

  // Current LittleFS log file path (thread-safe; the writer mutates it).
  String getCurrentLogFilePath();

 private:
  Logger() = default;
  ~Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  // One queued record — fixed size so it can cross the FreeRTOS queue by value
  // (no heap-owning String, which memcpy would corrupt).
  struct QueuedLog {
    uint32_t timestampMs;
    LogLevel level;
    char tag[16];
    char body[200];
  };

  // Shared formatting body for the five level methods (collapses what was five
  // identical va_list blocks).
  void vlog(LogLevel level, const char* tag, const char* format, va_list args);
  void enqueue(LogLevel level, const char* tag, const char* body);

  // Writer task and its helpers (all run on the LogWriter task).
  static void writerTask(void* param);
  void drainToFile();       // append every queued record to the current file
  bool writeHeaderIfNew();  // header for a freshly-created file; seeds size_; false on I/O failure
  void rotateFile();        // new filename + header + reset size counter
  // Format a "N dropped" notice into out and clear the counter; 0 if none.
  size_t buildDroppedNotice(char* out, size_t outSize);
  static String generateLogFileName();
  static size_t formatLine(char* out, size_t outSize, const QueuedLog& msg);

  // Configuration (read on the hot path from many tasks).
  std::atomic<LogLevel> minLogLevel_{LogLevel::WARN};
  std::atomic<bool> serialLoggingEnabled_{false};
  std::atomic<uint32_t> droppedCount_{0};

  // Queue + writer task
  QueueHandle_t logQueue_ = nullptr;
  TaskHandle_t writerHandle_ = nullptr;
  std::atomic<bool> initialized_{false};

  // Owned by the writer task, except currentLogFile_ which pathMutex_ guards.
  String currentLogFile_;
  size_t currentSize_ = 0;
  SemaphoreHandle_t pathMutex_ = nullptr;

  // Rotation handshake: rotateLogs() sets the flag and waits on rotateDone_.
  std::atomic<bool> rotateRequested_{false};
  SemaphoreHandle_t rotateDone_ = nullptr;

  // Constants
  static constexpr const char* LOG_PREFIX = "/log_";
  static constexpr const char* LOG_EXTENSION = ".txt";
  static constexpr size_t MAX_LOG_FILE_SIZE = 100 * 1024;  // rotate past 100 KB
  static constexpr size_t QUEUE_DEPTH = 24;
  static constexpr uint32_t WRITER_POLL_MS = 250;   // writer wakeup cadence
  static constexpr uint32_t ROTATE_TIMEOUT_MS = 2000;
};

// Convenience macros
#define LOG_DEBUG(tag, ...) Logger::getInstance().debug(tag, __VA_ARGS__)
#define LOG_INFO(tag, ...) Logger::getInstance().info(tag, __VA_ARGS__)
#define LOG_WARN(tag, ...) Logger::getInstance().warn(tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) Logger::getInstance().error(tag, __VA_ARGS__)
#define LOG_ROTATE() Logger::getInstance().rotateLogs()
#define LOG_CURRENT_PATH() Logger::getInstance().getCurrentLogFilePath()

#endif  // LOGGER_H
