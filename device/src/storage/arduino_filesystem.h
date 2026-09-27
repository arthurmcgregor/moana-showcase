#pragma once

#include <FS.h>
#include <esp_task_wdt.h>

#include <memory>

#include "storage/track_filesystem.h"

// Sequential write handle wrapping an open Arduino fs::File.
class ArduinoWriteFile : public TrackFileSystem::WriteFile {
 public:
  explicit ArduinoWriteFile(File file) : file_(file) {}

  bool write(const uint8_t* data, size_t len) override {
    return file_.write(data, len) == len;
  }

  // fs::File::flush() is void — it cannot report failure on hardware, so a
  // short write() above is the only disk-full signal available here.
  bool flush() override {
    file_.flush();
    return true;
  }

  void close() override { file_.close(); }

 private:
  File file_;
};

// Sequential read handle wrapping an open Arduino fs::File.
class ArduinoReadFile : public TrackFileSystem::ReadFile {
 public:
  explicit ArduinoReadFile(File file) : file_(file) {}

  size_t read(uint8_t* buf, size_t maxLen) override {
    if (!file_) return 0;
    int n = file_.readBytes(reinterpret_cast<char*>(buf), maxLen);
    return n > 0 ? static_cast<size_t>(n) : 0;
  }

  void close() override { file_.close(); }

 private:
  File file_;
};

// Base adapter for any Arduino fs::FS implementation.
// Subclasses only need to implement mount(), unmount(), format(), and provide the fs::FS ref.
class ArduinoFileSystem : public TrackFileSystem {
 public:
  explicit ArduinoFileSystem(fs::FS& fs) : fs_(fs) {}

  std::unique_ptr<WriteFile> openWrite(const String& path) override {
    File f = fs_.open(path, "w");
    if (!f) return nullptr;
    return std::unique_ptr<WriteFile>(new ArduinoWriteFile(f));
  }

  std::unique_ptr<WriteFile> openAppend(const String& path) override {
    File f = fs_.open(path, "a");
    if (!f) return nullptr;
    return std::unique_ptr<WriteFile>(new ArduinoWriteFile(f));
  }

  std::unique_ptr<ReadFile> openRead(const String& path) override {
    File f = fs_.open(path, "r");
    if (!f) return nullptr;
    return std::unique_ptr<ReadFile>(new ArduinoReadFile(f));
  }

  bool exists(const String& path) override { return fs_.exists(path); }
  bool remove(const String& path) override { return fs_.remove(path); }
  bool rename(const String& from, const String& to) override { return fs_.rename(from, to); }
  bool mkdir(const String& path) override {
    if (fs_.exists(path)) return true;
    return fs_.mkdir(path);
  }

  size_t fileSize(const String& path) override {
    File f = fs_.open(path, "r");
    if (!f) return 0;
    size_t s = f.size();
    f.close();
    return s;
  }

  std::vector<String> listDir(const String& path) override {
    std::vector<String> entries;
    File root = fs_.open(path);
    if (!root || !root.isDirectory()) return entries;

    File f = root.openNextFile();
    while (f) {
      String name = f.name();
      if (!name.startsWith("/")) {
        name = "/" + name;
      }
      entries.push_back(name);
      f.close();
      esp_task_wdt_reset();
      f = root.openNextFile();
    }
    root.close();
    return entries;
  }

  size_t readFile(const String& path, ReadVisitor visitor) override {
    File f = fs_.open(path, "r");
    if (!f) return 0;

    size_t total = 0;
    uint8_t buf[512];
    while (f.available()) {
      size_t n = f.readBytes((char*)buf, sizeof(buf));
      // available() can stay true when the underlying read errors (e.g.
      // corrupt flash blocks) — a zero-length read would otherwise spin this
      // loop forever and trip the task watchdog. Callers detect the short
      // copy from the returned total.
      if (n == 0) break;
      total += n;
      if (!visitor(buf, n)) break;  // visitor aborted (e.g. write failed)
    }
    f.close();
    return total;
  }

  bool writeFile(const String& path, const uint8_t* data, size_t len) override {
    File f = fs_.open(path, "w");
    if (!f) return false;
    size_t written = f.write(data, len);
    f.close();
    return written == len;
  }

  bool writeFileStreamed(const String& path, WriterFn writer) override {
    File f = fs_.open(path, "w");
    if (!f) return false;
    bool ok = true;
    writer([&f, &ok](const uint8_t* data, size_t len) -> bool {
      size_t written = f.write(data, len);
      if (written != len) ok = false;
      return ok;
    });
    f.close();
    return ok;
  }

 protected:
  fs::FS& fs_;
};
