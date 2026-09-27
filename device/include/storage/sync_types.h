#pragma once

#include <functional>

struct SyncResult {
  int filesSucceeded = 0;
  int filesFailed = 0;
  int logFilesSucceeded = 0;
  int logFilesFailed = 0;
};

using SyncCompleteFn = std::function<void(const SyncResult&)>;
