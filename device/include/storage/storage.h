#pragma once

#include <Arduino.h>

#include <atomic>
#include <functional>
#include <vector>

#include "storage/sync_types.h"
#include "storage/track_filesystem.h"

// Serialises SD (SPI-bus) access against display drawing on the shared FSPI
// bus (B4). Injected by App from Display's SPI mutex; no-op when unset (tests,
// or contexts with no display). Held per SD transaction, never across a whole
// sync — a multi-second hold would stall the app-controller task and risk a
// dropped SYNC_COMPLETE (R3).
using SpiLockFn = std::function<void()>;

// Outcome of an archive listing. Lets the web layer tell a genuinely empty
// archive (Ok with no tracks) apart from an SD mount failure or a busy archive
// — an empty vector alone is ambiguous, and a client reads an empty 200 as
// "no tracks" and clears its list.
enum class ArchiveStatus { Ok, MountFailed, Busy };

// One archived track and its on-disk size in bytes, so the web layer can report
// sizes in the track list without a second round-trip per file.
struct TrackInfo {
  String name;
  size_t size = 0;
};

struct ArchiveListing {
  ArchiveStatus status = ArchiveStatus::MountFailed;
  std::vector<TrackInfo> tracks;
  bool ok() const { return status == ArchiveStatus::Ok; }
};

// RAII handle for a track file open on an archive filesystem.
// Keeps the filesystem mounted while alive. Move-only.
class TrackFile {
 public:
  using ReleaseFn = std::function<void()>;

  TrackFile() = default;
  ~TrackFile();

  TrackFile(TrackFile&& other) noexcept;
  TrackFile& operator=(TrackFile&& other) noexcept;
  TrackFile(const TrackFile&) = delete;
  TrackFile& operator=(const TrackFile&) = delete;

  explicit operator bool() const { return size_ > 0; }
  size_t size() const { return size_; }

  // Pull the next up-to-maxLen bytes into buf (0 at EOF). Opens the underlying
  // read handle lazily on first call. Each call takes the SPI lock, so it is
  // safe to drive from the AsyncWebServer chunked-response filler (F4).
  size_t readChunk(uint8_t* buf, size_t maxLen);

 private:
  friend class Storage;
  TrackFile(size_t size, TrackFileSystem* fs, const String& path, ReleaseFn onRelease,
            SpiLockFn spiLock = nullptr, SpiLockFn spiUnlock = nullptr);

  size_t size_ = 0;
  TrackFileSystem* fs_ = nullptr;
  String path_;
  ReleaseFn onRelease_;
  SpiLockFn spiLock_;
  SpiLockFn spiUnlock_;
  std::unique_ptr<TrackFileSystem::ReadFile> reader_;  // opened lazily by readChunk()
};

class Storage {
 public:
  // Production constructor — creates real LittleFS + SD adapters
  Storage();
  ~Storage();

  // Test constructor — inject fake filesystems
  Storage(TrackFileSystem* local, TrackFileSystem* archive);

  // Serialise SD access against display drawing on the shared SPI bus (B4).
  // Both must be set together; unset leaves SD access unserialised (tests).
  void setSpiLock(SpiLockFn lock, SpiLockFn unlock);

  // Local tracks (LittleFS — always mounted)
  std::vector<String> localTracks();
  bool formatLocal();

  // Archived tracks (SD — mounted on demand). The status distinguishes a
  // genuine empty archive from a mount failure / busy archive (see above).
  ArchiveListing archivedTracks();
  bool deleteArchived(const String& filename);
  TrackFile openArchived(const String& filename);

  // Sync (local → archive, async)
  SyncResult syncToArchive(bool deleteAfterSync);
  bool startSync(bool deleteAfterSync, SyncCompleteFn onComplete);
  bool isSyncing() const;

 private:
  TrackFileSystem* local_;
  TrackFileSystem* archive_;
  bool ownsFilesystems_ = false;
  std::atomic<bool> syncing_{false};
  // Count of live web archive operations (list/delete, and each open TrackFile
  // for its whole lifetime). Together with syncing_ this is a two-flag mutual
  // exclusion over the SD mount: each side announces itself, then checks the
  // other, so neither a sync nor a web op can tear the mount out from under a
  // live operation in the other direction (B4).
  std::atomic<int> archiveUsers_{0};
  SpiLockFn spiLock_;
  SpiLockFn spiUnlock_;

  // RAII claim on the SD mount for a web archive operation. Announces itself
  // (++archiveUsers_) then checks syncing_; if a sync already owns the mount it
  // backs the count out and reports not-granted. On destruction it releases the
  // claim — unless keep() transferred ownership to a longer-lived holder (an
  // open TrackFile, which releases it when destroyed).
  class ArchiveAccess {
   public:
    explicit ArchiveAccess(Storage* s) : storage_(s) {
      storage_->archiveUsers_.fetch_add(1);
      granted_ = !storage_->syncing_.load();
      if (!granted_) storage_->archiveUsers_.fetch_sub(1);
    }
    ~ArchiveAccess() {
      if (granted_ && !kept_) storage_->archiveUsers_.fetch_sub(1);
    }
    ArchiveAccess(const ArchiveAccess&) = delete;
    ArchiveAccess& operator=(const ArchiveAccess&) = delete;

    bool granted() const { return granted_; }
    // Hand the claim to a TrackFile: the destructor no longer releases it; the
    // TrackFile's onRelease does, after it unmounts.
    void keep() { kept_ = true; }

   private:
    Storage* storage_;
    bool granted_ = false;
    bool kept_ = false;
  };

  // RAII SPI-bus lock for one SD transaction. No-op when the lock is unset.
  class SpiSection {
   public:
    explicit SpiSection(Storage* s) : storage_(s) {
      if (storage_->spiLock_) storage_->spiLock_();
    }
    ~SpiSection() {
      if (storage_->spiUnlock_) storage_->spiUnlock_();
    }
    SpiSection(const SpiSection&) = delete;
    SpiSection& operator=(const SpiSection&) = delete;

   private:
    Storage* storage_;
  };

  struct SyncContext {
    Storage* storage;
    bool deleteAfterSync;
    SyncCompleteFn onComplete;
  };

  static void syncTask(void* pvParameters);

  // Internal helpers
  std::vector<String> listTracks(TrackFileSystem* fs);
  std::vector<String> listLocalEventSidecars();
  std::vector<String> listLocalLogs();
  bool copyToArchive(const String& srcPath, const String& destPath);
  void syncLogFiles(SyncResult& result, const std::vector<String>& logs,
                    const String& activeLogPath);
};
