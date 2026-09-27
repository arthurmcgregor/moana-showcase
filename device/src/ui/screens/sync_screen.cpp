#include "ui/screens/sync_screen.h"

#include <M5Unified.h>

#include "ui/screen_context.h"
#include "ui/screens/touch_targets.h"
#include "ui/screens/screen_shared.h"
#include "ui/screens/screen_widgets.h"
#include "ui/text_style.h"
#include "utils/logger.h"

static const char* LOG_TAG = "SyncScreen";

SyncScreen::SyncScreen() {}

void SyncScreen::init(ScreenContext* ctx) {
  LOG_INFO(LOG_TAG, "SyncScreen::init called");
  ctx_ = ctx;
  buttonWidth = M5.Display.width() - 40;  // 20px padding on each side
  buttonHeight = 80;
}

void SyncScreen::render() {
  // Fresh entry is a clean slate — clear any transient "archive busy" prompt.
  // A defer can only occur while this screen is already active (AppController
  // gates startSync on SYNC_SCREEN), so this never drops a just-set message.
  deferred_ = false;

  // Re-seed the track count from storage on every entry; AppController used
  // to do this as a side effect of the SYNC button. After Phase 4 of the
  // ScreenContext refactor, ctx_->show() is the only entry path.
  if (currentSyncState == SyncState::IDLE) {
    currentTrackCount = ctx_->localTrackCount();
  }
  renderFull(currentSyncState, currentTrackCount);
}

void SyncScreen::onSyncStateChanged(SyncState syncState, int trackCount) {
  if (syncState == currentSyncState && trackCount == currentTrackCount) {
    return;
  }
  currentSyncState = syncState;
  currentTrackCount = trackCount;
  if (syncState == SyncState::SYNCING) {
    hasLastResult = false;
    deferred_ = false;  // a real start supersedes the busy prompt
  }

  // Display broadcasts this hook to every screen (B12); only paint when we're
  // the visible screen. When inactive we just keep the cache current so
  // render() shows the right thing on re-entry.
  if (ctx_->current() != ScreenState::SYNC_SCREEN) {
    return;
  }

  if (syncState == SyncState::IDLE) {
    renderFull(syncState, trackCount);
  } else if (syncState == SyncState::SYNCING) {
    drawSyncButton("Syncing...", ButtonColor::ButtonGrey);
  }
}

void SyncScreen::onSyncResult(const SyncResult& result) {
  lastResult = result;
  hasLastResult = true;
  deferred_ = false;  // a completed sync supersedes the busy prompt
  if (ctx_->current() != ScreenState::SYNC_SCREEN) {
    return;
  }
  renderFull(currentSyncState, currentTrackCount);
}

void SyncScreen::onSyncDeferred() {
  // The request was refused (SD mount held by a web archive op). State stays
  // IDLE; show a retry prompt. Broadcast to every screen (B12) — only paint
  // when visible. Only reachable while this screen is active anyway.
  deferred_ = true;
  currentSyncState = SyncState::IDLE;
  hasLastResult = false;  // busy prompt takes precedence over an old result
  currentTrackCount = ctx_->localTrackCount();
  if (ctx_->current() != ScreenState::SYNC_SCREEN) {
    return;
  }
  renderFull(currentSyncState, currentTrackCount);
}

void SyncScreen::renderFull(SyncState syncState, int trackCount) {
  currentSyncState = syncState;
  currentTrackCount = trackCount;

  ScreenWidgets::clearContent();

  // Draw title
  ui::applyTextStyle(ui::TextStyle::Title);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(top_center);
  M5.Display.drawString("Sync to SD Card", M5.Display.width() / 2, STATUS_BAR_HEIGHT + 20);

  // Draw status line: either the last sync result or the pending track count
  ui::applyTextStyle(ui::TextStyle::Body);
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.setTextDatum(top_center);

  char statusText[96];
  if (deferred_) {
    M5.Display.setTextColor(TFT_YELLOW);
    snprintf(statusText, sizeof(statusText), "Archive busy - try again");
  } else if (hasLastResult) {
    int totalFailed = lastResult.filesFailed + lastResult.logFilesFailed;
    if (totalFailed > 0) {
      M5.Display.setTextColor(TFT_YELLOW);
      snprintf(statusText, sizeof(statusText), "Synced %d tracks, %d logs (%d failed)",
               lastResult.filesSucceeded, lastResult.logFilesSucceeded, totalFailed);
    } else {
      snprintf(statusText, sizeof(statusText), "Synced %d tracks, %d logs",
               lastResult.filesSucceeded, lastResult.logFilesSucceeded);
    }
  } else {
    snprintf(statusText, sizeof(statusText), "Files to sync: %d", trackCount);
  }
  M5.Display.drawString(statusText, M5.Display.width() / 2, STATUS_BAR_HEIGHT + 70);

  // Draw sync button
  drawSyncButton("Start Sync", trackCount > 0 ? ButtonColor::ButtonGreen : ButtonColor::ButtonGrey);
}


void SyncScreen::handleClick(int x, int y) {
  int buttonX = 20;
  int buttonY = STATUS_BAR_HEIGHT + 120;

  if (TouchTargets::isInsideButton(x, y, buttonX, buttonY, buttonWidth, buttonHeight)) {
    LOG_DEBUG(LOG_TAG, "Sync button pressed");

    if (currentTrackCount > 0) {
      ctx_->requestSync();
    } else {
      LOG_INFO(LOG_TAG, "No files to sync");
    }
  }
}

void SyncScreen::handleHold(int x, int y) {
  LOG_INFO(LOG_TAG, "Sync screen held - return to menu");
  ctx_->back();
}

void SyncScreen::drawSyncButton(const char* label, ButtonColor color) {
  ScreenWidgets::drawFilledButton(20, STATUS_BAR_HEIGHT + 120, buttonWidth, buttonHeight, label,
                                  color);
}
