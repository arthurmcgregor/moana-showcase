#pragma once

#include "ui/screens/screen.h"
#include "ui/screens/screen_shared.h"
#include "types.h"

class SyncScreen : public Screen {
 public:
  SyncScreen();
  void init(ScreenContext* ctx) override;
  void render() override;
  void handleClick(int x, int y) override;
  void handleHold(int x, int y) override;

  void onSyncStateChanged(SyncState state, int trackCount) override;
  void onSyncResult(const SyncResult& result) override;
  void onSyncDeferred() override;

 private:
  void renderFull(SyncState syncState, int trackCount);
  void drawSyncButton(const char* label, ButtonColor color);

  ScreenContext* ctx_ = nullptr;
  SyncState currentSyncState = SyncState::IDLE;
  int currentTrackCount = 0;
  bool hasLastResult = false;
  SyncResult lastResult{};
  // Set when a sync request was refused because the SD mount was busy; shown
  // as a "try again" prompt. Transient — cleared on entry, on a real start,
  // or when a result arrives.
  bool deferred_ = false;
  int buttonWidth;
  int buttonHeight;
};
