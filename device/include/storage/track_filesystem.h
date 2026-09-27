#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Abstract filesystem port for track file operations.
// Production adapters wrap LittleFS or SD; test fakes use in-memory storage.
class TrackFileSystem {
 public:
  virtual ~TrackFileSystem() = default;

  // Sequential write handle to an open file, returned by openWrite().
  // Unlike writeFile()/writeFileStreamed() (whole-file, single call), this
  // stays open across many appends so a long-lived producer (the recorder's
  // task loop) can flush batches as they arrive. write()/flush() return false
  // on I/O failure — the signal the recorder uses to detect a full filesystem.
  class WriteFile {
   public:
    virtual ~WriteFile() = default;
    virtual bool write(const uint8_t* data, size_t len) = 0;
    virtual bool flush() = 0;
    virtual void close() = 0;
  };

  // Open (creating/truncating) a file for sequential writing.
  // Returns nullptr if the file could not be opened.
  virtual std::unique_ptr<WriteFile> openWrite(const String& path) = 0;

  // Open a file for appending (creating it if absent), positioned at the
  // end. Used for the events sidecar (issue #139), where each rare event is
  // an open-append-close so no handle is held across tasks.
  virtual std::unique_ptr<WriteFile> openAppend(const String& path) = 0;

  // Sequential read handle to an open file, returned by openRead(). Unlike
  // readFile() (whole-file, push-based visitor), this is pull-based so a
  // consumer that is itself pulled — the AsyncWebServer chunked-response filler
  // — can read one buffer at a time without holding the whole file in RAM (F4).
  class ReadFile {
   public:
    virtual ~ReadFile() = default;
    // Read up to maxLen bytes from the current position into buf. Returns the
    // number of bytes read; 0 at end-of-file.
    virtual size_t read(uint8_t* buf, size_t maxLen) = 0;
    virtual void close() = 0;
  };

  // Open an existing file for sequential reading.
  // Returns nullptr if the file could not be opened.
  virtual std::unique_ptr<ReadFile> openRead(const String& path) = 0;

  // Lifecycle
  virtual bool mount() = 0;
  virtual void unmount() = 0;
  virtual bool format() = 0;

  // File operations
  virtual bool exists(const String& path) = 0;
  virtual bool remove(const String& path) = 0;
  virtual bool rename(const String& from, const String& to) = 0;
  virtual size_t fileSize(const String& path) = 0;
  virtual bool mkdir(const String& path) = 0;

  // Directory listing — returns filenames (with leading /)
  virtual std::vector<String> listDir(const String& path) = 0;

  // Bulk read — calls visitor with chunks until EOF, or until the visitor
  // returns false to abort early (e.g. when the downstream write failed, so
  // there is no point streaming the rest of the file into a dead writer — R10).
  // Returns total bytes read from the source, or 0 on failure.
  using ReadVisitor = std::function<bool(const uint8_t* data, size_t len)>;
  virtual size_t readFile(const String& path, ReadVisitor visitor) = 0;

  // Write — writes data to a file (overwrites if exists).
  // Returns true on success.
  virtual bool writeFile(const String& path, const uint8_t* data, size_t len) = 0;

  // Streamed write — opens file for writing and calls the writer callback,
  // which receives a function to push chunks. Returns true on success.
  using ChunkWriter = std::function<bool(const uint8_t* data, size_t len)>;
  using WriterFn = std::function<void(ChunkWriter write)>;
  virtual bool writeFileStreamed(const String& path, WriterFn writer) = 0;
};
