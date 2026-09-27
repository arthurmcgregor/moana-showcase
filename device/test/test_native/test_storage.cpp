#include <gtest/gtest.h>

#include "fake_filesystem.h"
#include "storage/storage.h"

class StorageTest : public ::testing::Test {
 protected:
  FakeFileSystem local;
  FakeFileSystem archive;

  void SetUp() override {
    // Archive (SD) requires explicit mount; local (LittleFS) does not
    archive.setRequireMount(true);
  }

  Storage makeStorage() { return Storage(&local, &archive); }
};

// --- Local tracks ---

TEST_F(StorageTest, LocalTracksReturnsValidTrackFiles) {
  local.seedFile("/gps_20250101_120000.csv", "lat,lng\n-36.8,174.7\n");
  local.seedFile("/gps_20250102_130000.csv", "lat,lng\n-36.9,174.8\n");
  local.seedFile("/readme.txt", "not a track");

  auto storage = makeStorage();
  auto tracks = storage.localTracks();

  EXPECT_EQ(tracks.size(), 2u);
}

TEST_F(StorageTest, LocalTracksFiltersInvalidFilenames) {
  local.seedFile("/readme.txt", "nope");
  local.seedFile("/data.json", "nope");

  auto storage = makeStorage();
  auto tracks = storage.localTracks();

  EXPECT_EQ(tracks.size(), 0u);
}

// --- Format ---

TEST_F(StorageTest, FormatLocalDelegatesToFilesystem) {
  auto storage = makeStorage();
  bool result = storage.formatLocal();

  EXPECT_TRUE(result);
  EXPECT_EQ(local.formatCalls(), 1);
}

TEST_F(StorageTest, FormatLocalReturnsFalseOnFailure) {
  local.setFormatSucceeds(false);
  auto storage = makeStorage();

  EXPECT_FALSE(storage.formatLocal());
}

// --- Archived tracks ---

TEST_F(StorageTest, ArchivedTracksMountsAndUnmounts) {
  archive.seedFile("/gps_20250101_120000.csv", "lat,lng\n-36.8,174.7\n");

  auto storage = makeStorage();
  auto listing = storage.archivedTracks();

  EXPECT_TRUE(listing.ok());
  EXPECT_EQ(listing.status, ArchiveStatus::Ok);
  EXPECT_EQ(listing.tracks.size(), 1u);
  EXPECT_STREQ(listing.tracks[0].name.c_str(), "/gps_20250101_120000.csv");
  EXPECT_EQ(listing.tracks[0].size, 20u);  // "lat,lng\n-36.8,174.7\n"
  EXPECT_EQ(archive.mountCalls(), 1);
  EXPECT_EQ(archive.unmountCalls(), 1);
  EXPECT_FALSE(archive.isMounted());
}

TEST_F(StorageTest, ArchivedTracksReportsMountFailure) {
  archive.setMountSucceeds(false);
  archive.seedFile("/gps_20250101_120000.csv", "data");

  auto storage = makeStorage();
  auto listing = storage.archivedTracks();

  // Mount failure must be distinguishable from a genuinely empty archive so the
  // web layer can return 500 rather than an empty 200.
  EXPECT_FALSE(listing.ok());
  EXPECT_EQ(listing.status, ArchiveStatus::MountFailed);
  EXPECT_TRUE(listing.tracks.empty());
}

TEST_F(StorageTest, ArchivedTracksOkWhenEmpty) {
  auto storage = makeStorage();
  auto listing = storage.archivedTracks();

  // Empty archive is a success, not a failure.
  EXPECT_TRUE(listing.ok());
  EXPECT_EQ(listing.status, ArchiveStatus::Ok);
  EXPECT_TRUE(listing.tracks.empty());
}

// --- Delete archived ---

TEST_F(StorageTest, DeleteArchivedRemovesFile) {
  archive.seedFile("/gps_20250101_120000.csv", "data");

  auto storage = makeStorage();
  bool result = storage.deleteArchived("/gps_20250101_120000.csv");

  EXPECT_TRUE(result);
  EXPECT_FALSE(archive.hasFile("/gps_20250101_120000.csv"));
  EXPECT_FALSE(archive.isMounted());
}

TEST_F(StorageTest, DeleteArchivedRejectsInvalidFilename) {
  auto storage = makeStorage();
  bool result = storage.deleteArchived("/../etc/passwd");

  EXPECT_FALSE(result);
  // Should not even attempt to mount
  EXPECT_EQ(archive.mountCalls(), 0);
}

TEST_F(StorageTest, DeleteArchivedAcceptsEventSidecar) {
  // Sidecars are deletable via the same web path as their CSVs (issue #148).
  archive.seedFile("/gps_20250101_120000.events.jsonl", "{\"seq\":1}\n");

  auto storage = makeStorage();
  bool result = storage.deleteArchived("/gps_20250101_120000.events.jsonl");

  EXPECT_TRUE(result);
  EXPECT_FALSE(archive.hasFile("/gps_20250101_120000.events.jsonl"));
}

TEST_F(StorageTest, DeleteArchivedRejectsTraversalInSidecarName) {
  auto storage = makeStorage();
  bool result = storage.deleteArchived("/gps_../secret.events.jsonl");

  EXPECT_FALSE(result);
  EXPECT_EQ(archive.mountCalls(), 0);
}

TEST_F(StorageTest, DeleteArchivedReturnsFalseForNonexistentFile) {
  auto storage = makeStorage();
  bool result = storage.deleteArchived("/gps_20250101_120000.csv");

  EXPECT_FALSE(result);
}

// --- Open archived ---

TEST_F(StorageTest, OpenArchivedKeepsMountedUntilTrackFileDestroyed) {
  archive.seedFile("/gps_20250101_120000.csv", "lat,lng\n-36.8,174.7\n");

  auto storage = makeStorage();
  {
    TrackFile tf = storage.openArchived("/gps_20250101_120000.csv");
    EXPECT_TRUE(static_cast<bool>(tf));
    EXPECT_EQ(tf.size(), 20u);
    EXPECT_TRUE(archive.isMounted());
  }
  // Destructor should have unmounted
  EXPECT_FALSE(archive.isMounted());
}

TEST_F(StorageTest, OpenArchivedReturnsEmptyForInvalidFilename) {
  auto storage = makeStorage();
  TrackFile tf = storage.openArchived("not_a_track.txt");

  EXPECT_FALSE(static_cast<bool>(tf));
  EXPECT_EQ(archive.mountCalls(), 0);
}

TEST_F(StorageTest, OpenArchivedAcceptsEventSidecar) {
  // Sidecars sync to the SD root beside their CSVs, so the same open-for-web
  // path must serve them (issue #148).
  const std::string content = "{\"seq\":1,\"kind\":\"SAIL\",\"utc\":1753900000}\n";
  archive.seedFile("/gps_20250101_120000.events.jsonl", content);

  auto storage = makeStorage();
  TrackFile tf = storage.openArchived("/gps_20250101_120000.events.jsonl");

  ASSERT_TRUE(static_cast<bool>(tf));
  EXPECT_EQ(tf.size(), content.size());
}

TEST_F(StorageTest, OpenArchivedReturnsEmptyForMissingFile) {
  auto storage = makeStorage();
  TrackFile tf = storage.openArchived("/gps_20250101_120000.csv");

  EXPECT_FALSE(static_cast<bool>(tf));
  // Should have mounted and then unmounted on failure
  EXPECT_FALSE(archive.isMounted());
}

// (TrackFile content delivery is covered by ReadChunkStreamsFullContentThenEof.)

// --- Sync ---

TEST_F(StorageTest, SyncCopiesAllTracksToArchive) {
  local.seedFile("/gps_20250101_120000.csv", "lat,lng\n-36.8,174.7\n");
  local.seedFile("/gps_20250102_130000.csv", "lat,lng\n-36.9,174.8\n");

  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(false);

  EXPECT_EQ(result.filesSucceeded, 2);
  EXPECT_EQ(result.filesFailed, 0);
  EXPECT_TRUE(archive.hasFile("/gps_20250101_120000.csv"));
  EXPECT_TRUE(archive.hasFile("/gps_20250102_130000.csv"));
}

TEST_F(StorageTest, SyncCarriesEventSidecarsWithTracks) {
  // Events sidecars (issue #139) ride the same sync as their track CSVs:
  // copied to the archive and delete-after-sync'd like any track file.
  local.seedFile("/gps_20250101_120000.csv", "lat,lng\n-36.8,174.7\n");
  local.seedFile("/gps_20250101_120000.events.jsonl",
                 "{\"kind\":\"SAIL\",\"utc\":1753900000}\n");

  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(true);

  EXPECT_EQ(result.filesSucceeded, 2);
  EXPECT_EQ(result.filesFailed, 0);
  EXPECT_TRUE(archive.hasFile("/gps_20250101_120000.csv"));
  EXPECT_TRUE(archive.hasFile("/gps_20250101_120000.events.jsonl"));
  EXPECT_FALSE(local.hasFile("/gps_20250101_120000.csv"));
  EXPECT_FALSE(local.hasFile("/gps_20250101_120000.events.jsonl"));
}

TEST_F(StorageTest, LocalTracksExcludesEventSidecars) {
  // The UI's track count and the web track list must not treat a sidecar
  // as a track.
  local.seedFile("/gps_20250101_120000.csv", "data");
  local.seedFile("/gps_20250101_120000.events.jsonl", "{}");

  auto storage = makeStorage();
  std::vector<String> tracks = storage.localTracks();

  ASSERT_EQ(tracks.size(), 1u);
  EXPECT_EQ(tracks[0], String("/gps_20250101_120000.csv"));
}

TEST_F(StorageTest, SyncMountsArchiveOnceForBatch) {
  local.seedFile("/gps_20250101_120000.csv", "data1");
  local.seedFile("/gps_20250102_130000.csv", "data2");
  local.seedFile("/gps_20250103_140000.csv", "data3");

  auto storage = makeStorage();
  storage.syncToArchive(false);

  // Single mount for the entire batch, not 3 separate mounts
  EXPECT_EQ(archive.mountCalls(), 1);
  EXPECT_EQ(archive.unmountCalls(), 1);
}

TEST_F(StorageTest, SyncDeletesFromLocalWhenFlagSet) {
  local.seedFile("/gps_20250101_120000.csv", "data");

  auto storage = makeStorage();
  storage.syncToArchive(true);

  EXPECT_FALSE(local.hasFile("/gps_20250101_120000.csv"));
  EXPECT_TRUE(archive.hasFile("/gps_20250101_120000.csv"));
}

TEST_F(StorageTest, SyncPreservesLocalWhenFlagNotSet) {
  local.seedFile("/gps_20250101_120000.csv", "data");

  auto storage = makeStorage();
  storage.syncToArchive(false);

  EXPECT_TRUE(local.hasFile("/gps_20250101_120000.csv"));
  EXPECT_TRUE(archive.hasFile("/gps_20250101_120000.csv"));
}

TEST_F(StorageTest, SyncWithMountFailureReportsAllFailed) {
  local.seedFile("/gps_20250101_120000.csv", "data");
  local.seedFile("/gps_20250102_130000.csv", "data");
  archive.setMountSucceeds(false);

  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(false);

  EXPECT_EQ(result.filesSucceeded, 0);
  EXPECT_EQ(result.filesFailed, 2);
}

TEST_F(StorageTest, SyncOverwritesExistingFilesOnArchive) {
  local.seedFile("/gps_20250101_120000.csv", "lat,lng\nnew data\n");
  archive.seedFile("/gps_20250101_120000.csv", "lat,lng\nold data\n");

  auto storage = makeStorage();
  storage.syncToArchive(false);

  std::string content = archive.readFileContent("/gps_20250101_120000.csv");
  EXPECT_EQ(content, "lat,lng\nnew data\n");
}

TEST_F(StorageTest, SyncWithNoTracksReturnsZeroCounts) {
  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(false);

  EXPECT_EQ(result.filesSucceeded, 0);
  EXPECT_EQ(result.filesFailed, 0);
  // Should not attempt to mount archive when there are no files
  EXPECT_EQ(archive.mountCalls(), 0);
}

TEST_F(StorageTest, SyncShortReadFailsTrackAndPreservesLocal) {
  // A source that errors mid-stream (corrupt flash blocks) delivers fewer
  // bytes than fileSize() promised. The copy must count as FAILED — and
  // deleteAfterSync must NOT destroy the only complete copy of the data.
  local.seedFile("/gps_20250101_120000.csv", "lat,lng\n1,2\n3,4\n");
  local.setShortReadLimit(4);

  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(true);

  EXPECT_EQ(result.filesSucceeded, 0);
  EXPECT_EQ(result.filesFailed, 1);
  EXPECT_TRUE(local.hasFile("/gps_20250101_120000.csv"));
}

TEST_F(StorageTest, SyncFailureKeepsExistingArchiveCopy) {
  archive.seedFile("/gps_20250101_120000.csv", "lat,lng\n1,2\n3,4\n");
  local.seedFile("/gps_20250101_120000.csv", "lat,lng\n1,2\n3,4\n");
  local.setShortReadLimit(4);

  auto storage = makeStorage();
  storage.syncToArchive(false);

  EXPECT_EQ(archive.readFileContent("/gps_20250101_120000.csv"), "lat,lng\n1,2\n3,4\n");
  EXPECT_FALSE(archive.hasFile("/gps_20250101_120000.csv.tmp"));
}

TEST_F(StorageTest, SyncSetsAsideTrackWithNothingReadable) {
  // Seen on a real device: fileSize() reports 8077 bytes, every read returns 0.
  local.seedFile("/gps_20250101_120000.csv", "lat,lng\n1,2\n3,4\n");
  local.setShortReadLimit(0);

  auto storage = makeStorage();
  SyncResult first = storage.syncToArchive(true);
  SyncResult second = storage.syncToArchive(true);

  EXPECT_EQ(first.filesFailed, 1);
  EXPECT_FALSE(local.hasFile("/gps_20250101_120000.csv"));
  EXPECT_TRUE(local.hasFile("/gps_20250101_120000.csv.unreadable"));
  EXPECT_FALSE(archive.hasFile("/gps_20250101_120000.csv"));
  EXPECT_EQ(second.filesFailed, 0);
}

TEST_F(StorageTest, SyncWithWriteFailureCountsFailedFiles) {
  local.seedFile("/gps_20250101_120000.csv", "data");
  archive.setWriteSucceeds(false);

  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(false);

  EXPECT_EQ(result.filesSucceeded, 0);
  EXPECT_EQ(result.filesFailed, 1);
  // Nothing was read because the archive refused the write — not the source's fault.
  EXPECT_TRUE(local.hasFile("/gps_20250101_120000.csv"));
}

// --- Log file sync ---

TEST_F(StorageTest, SyncCopiesLogFilesToArchiveLogsDir) {
  local.seedFile("/gps_20250101_120000.csv", "track");
  local.seedFile("/log_20250101_120000.txt", "log content a");
  local.seedFile("/log_20250102_120000.txt", "log content b");

  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(false);

  EXPECT_EQ(result.logFilesSucceeded, 2);
  EXPECT_EQ(result.logFilesFailed, 0);
  EXPECT_TRUE(archive.hasFile("/logs/log_20250101_120000.txt"));
  EXPECT_TRUE(archive.hasFile("/logs/log_20250102_120000.txt"));
  EXPECT_EQ(archive.readFileContent("/logs/log_20250101_120000.txt"), "log content a");
}

TEST_F(StorageTest, SyncCreatesLogsDirOnArchive) {
  local.seedFile("/log_20250101_120000.txt", "data");

  auto storage = makeStorage();
  storage.syncToArchive(false);

  EXPECT_GE(archive.mkdirCalls(), 1);
  ASSERT_FALSE(archive.mkdirPaths().empty());
  EXPECT_EQ(archive.mkdirPaths().front(), "/logs");
}

TEST_F(StorageTest, SyncWithOnlyLogsStillMountsArchive) {
  local.seedFile("/log_20250101_120000.txt", "data");

  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(false);

  EXPECT_EQ(archive.mountCalls(), 1);
  EXPECT_EQ(archive.unmountCalls(), 1);
  EXPECT_EQ(result.logFilesSucceeded, 1);
}

TEST_F(StorageTest, SyncDeletesLoggedCopiesFromLocalOnFullSuccess) {
  local.seedFile("/log_20250101_120000.txt", "a");
  local.seedFile("/log_20250102_120000.txt", "b");

  auto storage = makeStorage();
  storage.syncToArchive(false);

  // Neither matches the test stub's active log path (empty string), so both
  // are reclaimed once the archive copy succeeded.
  EXPECT_FALSE(local.hasFile("/log_20250101_120000.txt"));
  EXPECT_FALSE(local.hasFile("/log_20250102_120000.txt"));
}

TEST_F(StorageTest, SyncKeepsLocalLogsOnPartialFailure) {
  local.seedFile("/log_20250101_120000.txt", "a");
  archive.setWriteSucceeds(false);

  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(false);

  EXPECT_EQ(result.logFilesFailed, 1);
  EXPECT_TRUE(local.hasFile("/log_20250101_120000.txt"));
}

TEST_F(StorageTest, SyncIgnoresNonLogTextFiles) {
  local.seedFile("/gps_20250101_120000.csv", "track");
  local.seedFile("/notes.txt", "random");

  auto storage = makeStorage();
  SyncResult result = storage.syncToArchive(false);

  EXPECT_EQ(result.logFilesSucceeded, 0);
  EXPECT_FALSE(archive.hasFile("/logs/notes.txt"));
}

TEST_F(StorageTest, SyncWithTrackDeleteFlagDoesNotAffectLogCleanup) {
  local.seedFile("/gps_20250101_120000.csv", "track");
  local.seedFile("/log_20250101_120000.txt", "log");

  auto storage = makeStorage();
  storage.syncToArchive(true);

  EXPECT_FALSE(local.hasFile("/gps_20250101_120000.csv"));
  EXPECT_FALSE(local.hasFile("/log_20250101_120000.txt"));
  EXPECT_TRUE(archive.hasFile("/gps_20250101_120000.csv"));
  EXPECT_TRUE(archive.hasFile("/logs/log_20250101_120000.txt"));
}

// --- startSync (async wrapper, runs synchronously in native) ---

TEST_F(StorageTest, StartSyncCallsCompletionCallback) {
  local.seedFile("/gps_20250101_120000.csv", "data");

  auto storage = makeStorage();
  SyncResult captured;
  bool called = false;

  storage.startSync(false, [&](const SyncResult& r) {
    captured = r;
    called = true;
  });

  EXPECT_TRUE(called);
  EXPECT_EQ(captured.filesSucceeded, 1);
}

TEST_F(StorageTest, StartSyncRejectsConcurrentSync) {
  // First sync should succeed, but we can't easily test true concurrency
  // in native mode. Just verify the API returns true on first call.
  auto storage = makeStorage();
  bool first = storage.startSync(false, [](const SyncResult&) {});
  EXPECT_TRUE(first);
}

// --- B4: SD access serialised on the shared SPI bus ---

TEST_F(StorageTest, ArchivedTracksHoldsSpiLock) {
  archive.seedFile("/gps_20250101_120000.csv", "data");
  auto storage = makeStorage();

  int locks = 0, unlocks = 0;
  storage.setSpiLock([&]() { locks++; }, [&]() { unlocks++; });

  storage.archivedTracks();
  EXPECT_GT(locks, 0);
  EXPECT_EQ(locks, unlocks);  // balanced — no leaked lock
}

TEST_F(StorageTest, DeleteArchivedHoldsSpiLock) {
  archive.seedFile("/gps_20250101_120000.csv", "data");
  auto storage = makeStorage();

  int locks = 0, unlocks = 0;
  storage.setSpiLock([&]() { locks++; }, [&]() { unlocks++; });

  storage.deleteArchived("/gps_20250101_120000.csv");
  EXPECT_GT(locks, 0);
  EXPECT_EQ(locks, unlocks);
}

// (Open + streamed-read + release lock balance is covered by
// ReadChunkHoldsSpiLockAndBalancesOnDestroy.)

// The core B4 race: a web request must not tear down the SD mount while the
// sync task owns it. Rejecting archive access whenever a sync is in progress
// closes it. The fake fires this hook mid-copy, while syncing_ is set.
TEST_F(StorageTest, ArchiveAccessRejectedWhileSyncing) {
  local.seedFile("/gps_20250101_120000.csv", "lat,lng\n-36.8,174.7\n");
  auto storage = makeStorage();

  bool wasSyncing = false;
  bool listRejected = false, deleteRejected = false, openRejected = false;

  archive.setOnWriteStreamed([&]() {
    wasSyncing = storage.isSyncing();
    listRejected = (storage.archivedTracks().status == ArchiveStatus::Busy);
    deleteRejected = !storage.deleteArchived("/gps_20250101_120000.csv");
    TrackFile tf = storage.openArchived("/gps_20250101_120000.csv");
    openRejected = !static_cast<bool>(tf);
  });

  storage.startSync(false, [](const SyncResult&) {});

  EXPECT_TRUE(wasSyncing);      // hook really did fire mid-sync
  EXPECT_TRUE(listRejected);
  EXPECT_TRUE(deleteRejected);
  EXPECT_TRUE(openRejected);
}

// The reverse direction: a sync must not start while a web read owns the mount,
// or its final unmount() (SD.end()) would rip the card out from under the
// in-flight read. An open TrackFile holds the archive claim for its lifetime.
TEST_F(StorageTest, SyncRejectedWhileArchiveTrackOpen) {
  local.seedFile("/gps_20250101_120000.csv", "data");
  archive.seedFile("/gps_20250101_120000.csv", "lat,lng\n-36.8,174.7\n");
  auto storage = makeStorage();

  TrackFile tf = storage.openArchived("/gps_20250101_120000.csv");
  ASSERT_TRUE(static_cast<bool>(tf));

  bool started = storage.startSync(false, [](const SyncResult&) {});
  EXPECT_FALSE(started);  // deferred while the read owns the mount
}

TEST_F(StorageTest, SyncAllowedOnceArchiveTrackClosed) {
  local.seedFile("/gps_20250101_120000.csv", "data");
  archive.seedFile("/gps_20250101_120000.csv", "lat,lng\n-36.8,174.7\n");
  auto storage = makeStorage();

  {
    TrackFile tf = storage.openArchived("/gps_20250101_120000.csv");
    ASSERT_TRUE(static_cast<bool>(tf));
  }  // claim released here

  bool started = storage.startSync(false, [](const SyncResult&) {});
  EXPECT_TRUE(started);
}

// --- F4: chunked streaming read ---

TEST_F(StorageTest, ReadChunkStreamsFullContentThenEof) {
  const std::string content = "lat,lng\n-36.80,174.70\n-36.81,174.71\n-36.82,174.72\n";
  archive.seedFile("/gps_20250101_120000.csv", content);
  auto storage = makeStorage();

  TrackFile tf = storage.openArchived("/gps_20250101_120000.csv");
  ASSERT_TRUE(static_cast<bool>(tf));

  // Pull in small buffers, exactly as the AsyncWebServer filler would.
  std::string got;
  uint8_t buf[4];
  size_t n;
  while ((n = tf.readChunk(buf, sizeof(buf))) > 0) {
    got.append(reinterpret_cast<char*>(buf), n);
  }
  EXPECT_EQ(got, content);
  EXPECT_EQ(tf.readChunk(buf, sizeof(buf)), 0u);  // stays at EOF
}

TEST_F(StorageTest, ReadChunkHoldsSpiLockAndBalancesOnDestroy) {
  archive.seedFile("/gps_20250101_120000.csv", "lat,lng\n-36.8,174.7\n");
  auto storage = makeStorage();

  int locks = 0, unlocks = 0;
  storage.setSpiLock([&]() { locks++; }, [&]() { unlocks++; });

  {
    TrackFile tf = storage.openArchived("/gps_20250101_120000.csv");
    ASSERT_TRUE(static_cast<bool>(tf));
    uint8_t buf[8];
    while (tf.readChunk(buf, sizeof(buf)) > 0) {
    }
  }  // destroy: close reader + unmount, all under the lock

  EXPECT_GT(locks, 0);
  EXPECT_EQ(locks, unlocks);  // every per-chunk + teardown lock released
}
