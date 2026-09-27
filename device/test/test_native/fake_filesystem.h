#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "storage/track_filesystem.h"

// Sequential write handle for the in-memory filesystem. Appends to a std::string
// owned by FakeFileSystem. Reads the write/flush success flags through pointers
// so a test can flip them mid-session to inject a failure on a later batch.
class FakeWriteFile : public TrackFileSystem::WriteFile {
 public:
  FakeWriteFile(std::string* content, const bool* writeOk, const bool* flushOk)
      : content_(content), writeOk_(writeOk), flushOk_(flushOk) {}

  bool write(const uint8_t* data, size_t len) override {
    if (!*writeOk_) return false;
    content_->append(reinterpret_cast<const char*>(data), len);
    return true;
  }

  bool flush() override { return *flushOk_; }

  void close() override { closed_ = true; }

  bool wasClosed() const { return closed_; }

 private:
  std::string* content_;
  const bool* writeOk_;
  const bool* flushOk_;
  bool closed_ = false;
};

// Sequential read handle over a copy of the file's bytes, with a position
// cursor so a test can pull it one chunk at a time (mirrors the device).
class FakeReadFile : public TrackFileSystem::ReadFile {
 public:
  explicit FakeReadFile(std::string content) : content_(std::move(content)) {}

  size_t read(uint8_t* buf, size_t maxLen) override {
    size_t remaining = content_.size() - pos_;
    size_t n = remaining < maxLen ? remaining : maxLen;
    for (size_t i = 0; i < n; i++) buf[i] = static_cast<uint8_t>(content_[pos_ + i]);
    pos_ += n;
    return n;
  }

  void close() override { closed_ = true; }
  bool wasClosed() const { return closed_; }

 private:
  std::string content_;
  size_t pos_ = 0;
  bool closed_ = false;
};

// In-memory filesystem for host-side testing.
// Tracks mount/unmount calls and supports fault injection.
class FakeFileSystem : public TrackFileSystem {
 public:
  // Lifecycle
  bool mount() override {
    mountCalls_++;
    if (!mountSucceeds_) return false;
    mounted_ = true;
    return true;
  }

  void unmount() override {
    unmountCalls_++;
    mounted_ = false;
  }

  bool format() override {
    formatCalls_++;
    if (!formatSucceeds_) return false;
    files_.clear();
    return true;
  }

  // File operations — enforce mount state when requireMount is set
  bool exists(const String& path) override {
    if (requireMount_ && !mounted_) return false;
    return files_.count(std::string(path.c_str())) > 0;
  }

  bool remove(const String& path) override {
    if (requireMount_ && !mounted_) return false;
    return files_.erase(std::string(path.c_str())) > 0;
  }

  // Fails onto an existing name, like FAT on the SD card.
  bool rename(const String& from, const String& to) override {
    if (requireMount_ && !mounted_) return false;
    auto it = files_.find(std::string(from.c_str()));
    if (it == files_.end() || files_.count(std::string(to.c_str()))) return false;
    files_[std::string(to.c_str())] = std::move(it->second);
    files_.erase(it);
    return true;
  }

  size_t fileSize(const String& path) override {
    if (requireMount_ && !mounted_) return 0;
    auto it = files_.find(std::string(path.c_str()));
    if (it == files_.end()) return 0;
    return it->second.size();
  }

  bool mkdir(const String& path) override {
    if (requireMount_ && !mounted_) return false;
    mkdirCalls_++;
    mkdirPaths_.push_back(std::string(path.c_str()));
    return true;
  }

  std::vector<String> listDir(const String& path) override {
    if (requireMount_ && !mounted_) return {};
    std::vector<String> entries;
    for (const auto& kv : files_) {
      entries.push_back(String(kv.first.c_str()));
    }
    return entries;
  }

  size_t readFile(const String& path, ReadVisitor visitor) override {
    if (requireMount_ && !mounted_) return 0;
    auto it = files_.find(std::string(path.c_str()));
    if (it == files_.end()) return 0;

    // shortReadLimit_ simulates a source that errors mid-stream (corrupt
    // flash blocks): fileSize() still reports the full size, but only the
    // first N bytes are ever delivered.
    const std::string& content = it->second;
    size_t n = content.size() < shortReadLimit_ ? content.size()
                                                : shortReadLimit_;
    visitor(reinterpret_cast<const uint8_t*>(content.data()), n);
    return n;
    // (single-chunk fake; visitor's return value is moot with one call)
  }

  void setShortReadLimit(size_t bytes) { shortReadLimit_ = bytes; }

  bool writeFile(const String& path, const uint8_t* data, size_t len) override {
    if (requireMount_ && !mounted_) return false;
    if (!writeSucceeds_) return false;
    files_[std::string(path.c_str())] = std::string(reinterpret_cast<const char*>(data), len);
    return true;
  }

  bool writeFileStreamed(const String& path, WriterFn writer) override {
    if (requireMount_ && !mounted_) return false;
    // Fires while the sync task owns the mount — a test uses it to re-enter
    // Storage and assert concurrent archive access is rejected (B4).
    if (onWriteStreamed_) onWriteStreamed_();
    if (!writeSucceeds_) return false;
    std::string& content = files_[std::string(path.c_str())];
    content.clear();
    writer([&content](const uint8_t* data, size_t len) -> bool {
      content.append(reinterpret_cast<const char*>(data), len);
      return true;
    });
    return true;
  }

  std::unique_ptr<WriteFile> openWrite(const String& path) override {
    if (requireMount_ && !mounted_) return nullptr;
    if (!openWriteSucceeds_) return nullptr;
    std::string& content = files_[std::string(path.c_str())];
    content.clear();
    return std::unique_ptr<WriteFile>(new FakeWriteFile(&content, &writeSucceeds_, &flushSucceeds_));
  }

  std::unique_ptr<WriteFile> openAppend(const String& path) override {
    if (requireMount_ && !mounted_) return nullptr;
    if (!openWriteSucceeds_) return nullptr;
    // Same as openWrite but keeps existing content (position at end).
    std::string& content = files_[std::string(path.c_str())];
    return std::unique_ptr<WriteFile>(new FakeWriteFile(&content, &writeSucceeds_, &flushSucceeds_));
  }

  std::unique_ptr<ReadFile> openRead(const String& path) override {
    if (requireMount_ && !mounted_) return nullptr;
    auto it = files_.find(std::string(path.c_str()));
    if (it == files_.end()) return nullptr;
    return std::unique_ptr<ReadFile>(new FakeReadFile(it->second));
  }

  // Test helpers
  void seedFile(const String& name, const std::string& content) {
    files_[std::string(name.c_str())] = content;
  }

  std::string readFileContent(const String& name) const {
    auto it = files_.find(std::string(name.c_str()));
    return (it != files_.end()) ? it->second : "";
  }

  // Direct file check — bypasses mount state (for test assertions)
  bool hasFile(const String& name) const {
    return files_.count(std::string(name.c_str())) > 0;
  }

  bool isMounted() const { return mounted_; }
  int mountCalls() const { return mountCalls_; }
  int unmountCalls() const { return unmountCalls_; }
  int formatCalls() const { return formatCalls_; }
  int mkdirCalls() const { return mkdirCalls_; }
  const std::vector<std::string>& mkdirPaths() const { return mkdirPaths_; }

  void setMountSucceeds(bool v) { mountSucceeds_ = v; }
  void setFormatSucceeds(bool v) { formatSucceeds_ = v; }
  void setWriteSucceeds(bool v) { writeSucceeds_ = v; }
  void setFlushSucceeds(bool v) { flushSucceeds_ = v; }
  void setOpenWriteSucceeds(bool v) { openWriteSucceeds_ = v; }
  void setRequireMount(bool v) { requireMount_ = v; }
  void setOnWriteStreamed(std::function<void()> fn) { onWriteStreamed_ = std::move(fn); }

 private:
  std::map<std::string, std::string> files_;
  bool mounted_ = false;
  int mountCalls_ = 0;
  int unmountCalls_ = 0;
  int formatCalls_ = 0;
  int mkdirCalls_ = 0;
  std::vector<std::string> mkdirPaths_;
  bool mountSucceeds_ = true;
  bool formatSucceeds_ = true;
  bool writeSucceeds_ = true;
  bool flushSucceeds_ = true;
  bool openWriteSucceeds_ = true;
  bool requireMount_ = false;
  size_t shortReadLimit_ = SIZE_MAX;
  std::function<void()> onWriteStreamed_;
};
