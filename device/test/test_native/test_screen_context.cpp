#include <gtest/gtest.h>

#include <vector>

#include "ui/display.h"
#include "ui/screen_context.h"
#include "fakes/fake_display_hardware.h"
#include "fakes/fake_scheduler.h"
#include "fakes/fake_screen.h"
#include "fakes/fake_touch_input.h"

class ScreenContextTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mainMenu.canEnterResult = true;
    speedScreen.canEnterResult = true;
    detailScreen.canEnterResult = true;

    display = std::make_unique<Display>(hw, touch, sched);
    display->registerScreen(ScreenState::MAIN_MENU, &mainMenu);
    display->registerScreen(ScreenState::SPEED_SCREEN, &speedScreen);
    display->registerScreen(ScreenState::DETAIL_SCREEN, &detailScreen);
    display->registerScreen(ScreenState::WIFI_SCREEN, &wifiScreen);
    display->registerScreen(ScreenState::SYNC_SCREEN, &syncScreen);

    context.init(
        display.get(),
        [this](ButtonId id) { posted.push_back(id); },
        [this]() { return fakeTrackCount; });

    display->init(&context, [this]() { return gpsState; });
  }

  FakeDisplayHardware hw;
  FakeTouchInput touch;
  FakeScheduler sched;
  std::unique_ptr<Display> display;
  ScreenContext context;

  FakeScreen mainMenu;
  FakeScreen speedScreen;
  FakeScreen detailScreen;
  FakeScreen wifiScreen;
  FakeScreen syncScreen;

  GPSState gpsState = GPSState::LOCKED;
  std::vector<ButtonId> posted;
  int fakeTrackCount = 0;
};

// --- Navigation runs synchronously through Display ---

TEST_F(ScreenContextTest, ShowTransitionsScreenWithoutPosting) {
  context.show(ScreenState::SPEED_SCREEN);

  EXPECT_EQ(display->getScreenState(), ScreenState::SPEED_SCREEN);
  EXPECT_TRUE(posted.empty());
}

TEST_F(ScreenContextTest, BackReturnsToMainMenu) {
  context.show(ScreenState::DETAIL_SCREEN);
  ASSERT_EQ(display->getScreenState(), ScreenState::DETAIL_SCREEN);

  context.back();

  EXPECT_EQ(display->getScreenState(), ScreenState::MAIN_MENU);
  EXPECT_TRUE(posted.empty());
}

TEST_F(ScreenContextTest, CurrentReflectsDisplayState) {
  EXPECT_EQ(context.current(), ScreenState::MAIN_MENU);
  context.show(ScreenState::SPEED_SCREEN);
  EXPECT_EQ(context.current(), ScreenState::SPEED_SCREEN);
}

// --- Commands post BUTTON_PRESS events; AppController owns the state machine ---

TEST_F(ScreenContextTest, ToggleRecordingPostsToggleButton) {
  context.toggleRecording();
  ASSERT_EQ(posted.size(), 1u);
  EXPECT_EQ(posted[0], ButtonId::TOGGLE_RECORDING);
}

TEST_F(ScreenContextTest, ToggleWifiPostsToggleWifi) {
  context.toggleWifi();
  ASSERT_EQ(posted.size(), 1u);
  EXPECT_EQ(posted[0], ButtonId::TOGGLE_WIFI);
}

TEST_F(ScreenContextTest, RequestSyncPostsStartSync) {
  context.requestSync();
  ASSERT_EQ(posted.size(), 1u);
  EXPECT_EQ(posted[0], ButtonId::START_SYNC);
}

TEST_F(ScreenContextTest, FormatPostsFormat) {
  context.format();
  ASSERT_EQ(posted.size(), 1u);
  EXPECT_EQ(posted[0], ButtonId::FORMAT);
}

// --- Read-only state ---

TEST_F(ScreenContextTest, LocalTrackCountForwardsToProvider) {
  fakeTrackCount = 7;
  EXPECT_EQ(context.localTrackCount(), 7);
  fakeTrackCount = 0;
  EXPECT_EQ(context.localTrackCount(), 0);
}

// R8: screens read the current fix state at render time (not just on the
// transition event) so entering during LOST paints the staleness cue.
TEST_F(ScreenContextTest, GpsStateForwardsCurrentFixState) {
  gpsState = GPSState::LOCKED;
  EXPECT_EQ(context.gpsState(), GPSState::LOCKED);
  gpsState = GPSState::LOST;
  EXPECT_EQ(context.gpsState(), GPSState::LOST);
}

