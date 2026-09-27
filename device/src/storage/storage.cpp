#include "storage/storage.h"

#include "storage/track_filename.h"
#include "utils/logger.h"

// Production adapters — only included in device builds (guarded by header internals)
#if defined(ARDUINO)
#include "littlefs_filesystem.h"
#include "sd_filesystem.h"
#endif

static const char* LOG_TAG = "Storage";

// ============================================
// TrackFile (RAII handle)
// ============================================

TrackFile::TrackFile(size_t size, TrackFileSystem* fs, const String& path, ReleaseFn onRelease,
                     SpiLockFn spiLock, SpiLockFn spiUnlock)
    : size_(size),
      fs_(fs),
      path_(path),
      onRelease_(std::move(onRelease)),
      spiLock_(std::move(spiLock)),
      spiUnlock_(std::move(spiUnlock)) {}

TrackFile::~TrackFile() {
  // Close the read handle (SD/SPI traffic) under the lock, before onRelease_
  // unmounts the card. No-op for moved-from / empty handles or handles that
  // were never streamed.
  if (reader_) {
    if (spiLock_) spiLock_();
    reader_->close();
    reader_.reset();
    if (spiUnlock_) spiUnlock_();
  }
  if (onRelease_) {
    onRelease_();
  }
}

TrackFile::TrackFile(TrackFile&& other) noexcept
    : size_(other.size_),
      fs_(other.fs_),
      path_(other.path_),
      onRelease_(std::move(other.onRelease_)),
      spiLock_(std::move(other.spiLock_)),
      spiUnlock_(std::move(other.spiUnlock_)),
      reader_(std::move(other.reader_)) {
  other.size_ = 0;
  other.fs_ = nullptr;
  other.onRelease_ = nullptr;
}

TrackFile& TrackFile::operator=(TrackFile&& other) noexcept {
  if (this != &other) {
    if (onRelease_) {
      onRelease_();
    }
    size_ = other.size_;
    fs_ = other.fs_;
    path_ = other.path_;
    onRelease_ = std::move(other.onRelease_);
    spiLock_ = std::move(other.spiLock_);
    spiUnlock_ = std::move(other.spiUnlock_);
    reader_ = std::move(other.reader_);
    other.size_ = 0;
    other.fs_ = nullptr;
    other.onRelease_ = nullptr;
  }
  return *this;
}

size_t TrackFile::readChunk(uint8_t* buf, size_t maxLen) {
  if (!fs_) return 0;
  if (spiLock_) spiLock_();
  if (!reader_) reader_ = fs_->openRead(path_);  // lazy open on first chunk
  size_t n = reader_ ? reader_->read(buf, maxLen) : 0;
  if (spiUnlock_) spiUnlock_();
  return n;
}

// ============================================
// Storage
// ============================================

#if defined(ARDUINO)
Storage::Storage()
    : local_(new LittleFSFileSystem()),
      archive_(new SDCardFileSystem()),
      ownsFilesystems_(true) {}
#else
Storage::Storage() : local_(nullptr), archive_(nullptr), ownsFilesystems_(false) {}
#endif

Storage::~Storage() {
  if (ownsFilesystems_) {
    delete local_;
    delete archive_;
  }
}

Storage::Storage(TrackFileSystem* local, TrackFileSystem* archive)
    : local_(local), archive_(archive), ownsFilesystems_(false) {}

void Storage::setSpiLock(SpiLockFn lock, SpiLockFn unlock) {
  spiLock_ = std::move(lock);
  spiUnlock_ = std::move(unlock);
}

// ============================================
// Local Track Operations (LittleFS)
// ============================================

std::vector<String> Storage::localTracks() {
  return listTracks(local_);
}

bool Storage::formatLocal() {
  LOG_INFO(LOG_TAG, "Formatting local filesystem...");
  return local_->format();
}

// ============================================
// Archived Track Operations (SD)
// ============================================

ArchiveListing Storage::archivedTracks() {
  // Claim the mount for the duration of this call — rejected if a sync owns it,
  // and blocks a sync from starting until we're done (B4).
  ArchiveAccess access(this);
  if (!access.granted()) {
    LOG_WARN(LOG_TAG, "Archive busy (sync in progress) — rejecting track list");
    return {ArchiveStatus::Busy, {}};
  }

  SpiSection spi(this);
  if (!archive_->mount()) {
    LOG_ERROR(LOG_TAG, "Archive mount failed");
    return {ArchiveStatus::MountFailed, {}};
  }

  // The archive is already mounted here, so a fileSize() per track is cheap
  // (one extra open/close each) and lets the web layer report sizes without a
  // second mount+scan.
  std::vector<String> names = listTracks(archive_);
  std::vector<TrackInfo> tracks;
  tracks.reserve(names.size());
  for (const String& name : names) {
    tracks.push_back({name, archive_->fileSize(name)});
  }
  archive_->unmount();
  return {ArchiveStatus::Ok, std::move(tracks)};
}

bool Storage::deleteArchived(const String& filename) {
  ArchiveAccess access(this);
  if (!access.granted()) {
    LOG_WARN(LOG_TAG, "Archive busy (sync in progress) — rejecting delete");
    return false;
  }

  // Event sidecars are deletable alongside their CSVs (issue #148) — a track
  // deleted from the web UI must not strand its sidecar on the card.
  String normalized = TrackFilename::normalize(filename);
  if (!TrackFilename::isValid(normalized) && !TrackFilename::isEventSidecar(normalized)) {
    LOG_ERROR(LOG_TAG, "Invalid track filename: %s", filename.c_str());
    return false;
  }

  SpiSection spi(this);
  if (!archive_->mount()) {
    return false;
  }

  bool success = archive_->remove(normalized);
  archive_->unmount();

  if (success) {
    LOG_INFO(LOG_TAG, "Deleted track: %s", normalized.c_str());
  } else {
    LOG_ERROR(LOG_TAG, "Failed to delete track: %s", normalized.c_str());
  }

  return success;
}

TrackFile Storage::openArchived(const String& filename) {
  // The returned TrackFile keeps the mount alive across the (seconds-long) web
  // read, so the claim must outlive this function: hold it via ArchiveAccess,
  // then keep() it and release in the TrackFile's onRelease. Early returns let
  // the ArchiveAccess destructor drop the claim.
  ArchiveAccess access(this);
  if (!access.granted()) {
    LOG_WARN(LOG_TAG, "Archive busy (sync in progress) — rejecting open");
    return {};
  }

  // Event sidecars are downloadable too (issue #148): they sync to the SD root
  // alongside their CSVs, and the frontend fetches them via the same endpoint.
  String normalized = TrackFilename::normalize(filename);
  if (!TrackFilename::isValid(normalized) && !TrackFilename::isEventSidecar(normalized)) {
    LOG_ERROR(LOG_TAG, "Invalid track filename: %s", filename.c_str());
    return {};
  }

  size_t size = 0;
  {
    SpiSection spi(this);
    if (!archive_->mount()) {
      return {};
    }

    size = archive_->fileSize(normalized);
    if (size == 0) {
      LOG_ERROR(LOG_TAG, "Failed to open track for streaming: %s", normalized.c_str());
      archive_->unmount();
      return {};
    }
  }

  LOG_INFO(LOG_TAG, "Opened track: %s (%d bytes)", normalized.c_str(), size);

  // TrackFile takes ownership of the archive mount + the archiveUsers_ claim —
  // its destructor unmounts and releases both. The unmount is SD/SPI traffic,
  // so wrap it in the lock too.
  access.keep();
  SpiLockFn lock = spiLock_;
  SpiLockFn unlock = spiUnlock_;
  auto onRelease = [this, lock, unlock]() {
    if (lock) lock();
    archive_->unmount();
    if (unlock) unlock();
    archiveUsers_.fetch_sub(1);
  };
  return TrackFile(size, archive_, normalized, onRelease, lock, unlock);
}

// ============================================
// Sync (local → archive)
// ============================================

bool Storage::startSync(bool deleteAfterSync, SyncCompleteFn onComplete) {
  if (syncing_.exchange(true)) {
    LOG_WARN(LOG_TAG, "Sync already in progress");
    return false;
  }

  // Other side of the two-flag guard: announce the sync (above), then check for
  // live web archive ops. If a TrackFile is open (or a list/delete is in
  // flight), starting now would unmount under it — back off and let it finish
  // (B4). Ordering mirrors ArchiveAccess (announce, then check the other flag),
  // which guarantees the two can't both proceed.
  if (archiveUsers_.load() != 0) {
    LOG_WARN(LOG_TAG, "Archive busy (web operation in progress) — deferring sync");
    syncing_.store(false);
    return false;
  }

  auto* ctx = new SyncContext{this, deleteAfterSync, std::move(onComplete)};
#if defined(ARDUINO)
  xTaskCreate(syncTask, "SyncTask", 8192, ctx, 2, nullptr);
#else
  // In native tests, run synchronously
  syncTask(ctx);
#endif
  return true;
}

void Storage::syncTask(void* pvParameters) {
  auto* ctx = static_cast<SyncContext*>(pvParameters);

  LOG_INFO(LOG_TAG, "Sync task started");
  SyncResult result = ctx->storage->syncToArchive(ctx->deleteAfterSync);

  LOG_INFO(LOG_TAG,
           "Sync completed: tracks %d/%d succeeded, logs %d/%d succeeded",
           result.filesSucceeded, result.filesSucceeded + result.filesFailed,
           result.logFilesSucceeded, result.logFilesSucceeded + result.logFilesFailed);

  // Release the sync's claim on the SD archive *before* invoking the completion
  // callback. The callback posts SYNC_COMPLETE with guaranteed delivery (R3),
  // which can block if the controller is momentarily stalled; clearing syncing_
  // first means web archive ops (list/download) recover immediately instead of
  // waiting behind that block. The SD work is already done by this point.
  ctx->storage->syncing_.store(false);
  ctx->onComplete(result);
  delete ctx;
#if defined(ARDUINO)
  vTaskDelete(nullptr);
#endif
}

bool Storage::isSyncing() const {
  return syncing_.load();
}

SyncResult Storage::syncToArchive(bool deleteAfterSync) {
  LOG_INFO(LOG_TAG, "Starting sync operation (deleteAfterSync=%d)", deleteAfterSync);

  std::vector<String> tracks = localTracks();
  std::vector<String> logs = listLocalLogs();

  // Events sidecars (issue #139) ride along with their tracks: copied and
  // delete-after-sync'd exactly like track CSVs, counted in the same
  // files* totals. Appended after the tracks so a mid-sync failure can't
  // leave a sidecar archived without its CSV having had its turn first.
  for (const String& sidecar : listLocalEventSidecars()) {
    tracks.push_back(sidecar);
  }

  if (tracks.empty() && logs.empty()) {
    LOG_INFO(LOG_TAG, "No tracks or logs found to sync");
    return {};
  }

  LOG_INFO(LOG_TAG, "Found %d track files (incl. event sidecars) and %d log files to sync",
           tracks.size(), logs.size());

  // Mount archive once for the entire batch. The SPI lock is taken per
  // transaction (mount, each file copy, unmount) rather than held across the
  // whole sync — a multi-second hold would stall the app-controller task and
  // could drop a SYNC_COMPLETE event, wedging sync until reboot (R3). Between
  // transactions the card stays mounted but idle, so display draws are safe.
  {
    SpiSection spi(this);
    if (!archive_->mount()) {
      LOG_ERROR(LOG_TAG, "Archive mount failed for sync");
      SyncResult result;
      result.filesFailed = static_cast<int>(tracks.size());
      result.logFilesFailed = static_cast<int>(logs.size());
      return result;
    }
  }

  SyncResult result;
  for (const String& track : tracks) {
    LOG_DEBUG(LOG_TAG, "Syncing track: %s", track.c_str());
    bool success;
    {
      SpiSection spi(this);
      success = copyToArchive(track, track);
    }
    if (!success) {
      LOG_ERROR(LOG_TAG, "Failed to copy track to archive: %s", track.c_str());
      result.filesFailed++;
    } else {
      LOG_INFO(LOG_TAG, "Copied track to archive: %s", track.c_str());
      result.filesSucceeded++;

      if (deleteAfterSync) {
        if (local_->remove(track)) {
          LOG_INFO(LOG_TAG, "Deleted track from local: %s", track.c_str());
        } else {
          LOG_WARN(LOG_TAG, "Failed to delete track from local: %s", track.c_str());
        }
      }
    }
  }

  String activeLogPath = LOG_CURRENT_PATH();
  syncLogFiles(result, logs, activeLogPath);

  // Unmount archive after entire batch
  {
    SpiSection spi(this);
    archive_->unmount();
  }

  // On fully successful log copy, rotate the active log and delete copied logs
  // from local to reclaim LittleFS space.
  if (result.logFilesSucceeded > 0 && result.logFilesFailed == 0) {
    LOG_INFO(LOG_TAG, "Rotating logs and cleaning up synced copies from local");
    LOG_ROTATE();
    for (const String& logPath : logs) {
      if (logPath == activeLogPath) continue;
      if (!local_->remove(logPath)) {
        LOG_WARN(LOG_TAG, "Failed to delete synced log from local: %s", logPath.c_str());
      }
    }
  }

  return result;
}

// ============================================
// Internal Helpers
// ============================================

std::vector<String> Storage::listTracks(TrackFileSystem* fs) {
  std::vector<String> entries = fs->listDir("/");
  std::vector<String> tracks;

  LOG_INFO(LOG_TAG, "Scanning for GPS tracks...");

  for (const String& entry : entries) {
    String normalized = TrackFilename::normalize(entry);
    if (TrackFilename::isValid(normalized)) {
      tracks.push_back(normalized);

      if (tracks.size() % 25 == 0) {
        LOG_INFO(LOG_TAG, "Found %d GPS tracks so far...", tracks.size());
      }
    }
  }

  LOG_INFO(LOG_TAG, "Found %d GPS tracks", tracks.size());
  return tracks;
}

std::vector<String> Storage::listLocalEventSidecars() {
  std::vector<String> entries = local_->listDir("/");
  std::vector<String> sidecars;
  for (const String& entry : entries) {
    String normalized = TrackFilename::normalize(entry);
    if (TrackFilename::isEventSidecar(normalized)) {
      sidecars.push_back(normalized);
    }
  }
  return sidecars;
}

std::vector<String> Storage::listLocalLogs() {
  std::vector<String> entries = local_->listDir("/");
  std::vector<String> logs;
  for (const String& entry : entries) {
    if (entry.startsWith("/log_") && entry.endsWith(".txt")) {
      logs.push_back(entry);
    }
  }
  return logs;
}

void Storage::syncLogFiles(SyncResult& result, const std::vector<String>& logs,
                           const String& activeLogPath) {
  if (logs.empty()) {
    return;
  }

  const String LOG_DIR = "/logs";
  {
    SpiSection spi(this);
    if (!archive_->mkdir(LOG_DIR)) {
      LOG_ERROR(LOG_TAG, "Failed to create log dir on archive: %s", LOG_DIR.c_str());
      result.logFilesFailed = static_cast<int>(logs.size());
      return;
    }
  }

  for (const String& logPath : logs) {
    if (logPath == activeLogPath) {
      LOG_DEBUG(LOG_TAG, "Skipping active log file: %s", logPath.c_str());
      continue;
    }
    String destPath = LOG_DIR + logPath;
    LOG_INFO(LOG_TAG, "Syncing log: %s -> %s", logPath.c_str(), destPath.c_str());
    SpiSection spi(this);
    bool success = archive_->writeFileStreamed(destPath, [this, &logPath](TrackFileSystem::ChunkWriter write) {
      local_->readFile(logPath, [&write](const uint8_t* data, size_t len) -> bool {
        return write(data, len);  // stop reading once a chunk write fails (R10)
      });
    });
    if (success) {
      result.logFilesSucceeded++;
    } else {
      LOG_ERROR(LOG_TAG, "Failed to copy log to archive: %s", logPath.c_str());
      result.logFilesFailed++;
    }
  }
}

bool Storage::copyToArchive(const String& srcPath, const String& destPath) {
  size_t srcSize = local_->fileSize(srcPath);
  if (srcSize == 0) {
    LOG_ERROR(LOG_TAG, "Failed to read source file: %s", srcPath.c_str());
    return false;
  }

  LOG_INFO(LOG_TAG, "Copying %s (%d bytes) -> %s", srcPath.c_str(), srcSize, destPath.c_str());

  // Stream into a temp file, so a failed copy never replaces a good archived one.
  String tmpPath = destPath + ".tmp";
  size_t copied = 0;
  bool success = archive_->writeFileStreamed(tmpPath, [this, &srcPath, &copied](TrackFileSystem::ChunkWriter write) {
    copied = local_->readFile(srcPath, [&write](const uint8_t* data, size_t len) -> bool {
      return write(data, len);  // stop reading once a chunk write fails (R10)
    });
  });

  // A short read (source error mid-stream) must fail the copy, or
  // deleteAfterSync would destroy the only good copy of the data. Growing
  // files (a track still being recorded) copy MORE than the size snapshot —
  // that's fine.
  if (success && copied < srcSize) {
    LOG_ERROR(LOG_TAG, "Short read syncing %s: %d of %d bytes", srcPath.c_str(),
              copied, srcSize);
    success = false;

    // Not one byte readable: it would fail every sync forever. Set it aside,
    // under a name sync ignores, rather than delete it.
    if (copied == 0) {
      String asidePath = srcPath + ".unreadable";
      if (local_->rename(srcPath, asidePath)) {
        LOG_WARN(LOG_TAG, "Set aside unreadable file: %s", asidePath.c_str());
      }
    }
  }

  // The SD card's FAT can't rename onto an existing name.
  if (success) {
    archive_->remove(destPath);
    success = archive_->rename(tmpPath, destPath);
  }

  if (!success) {
    archive_->remove(tmpPath);
    LOG_ERROR(LOG_TAG, "Failed to write destination file: %s", destPath.c_str());
  } else {
    LOG_INFO(LOG_TAG, "Copied %d bytes successfully", srcSize);
  }

  return success;
}
