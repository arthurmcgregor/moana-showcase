#include <gtest/gtest.h>

#include "ui/screens/touch_targets.h"

// CoreS3 display: 320x240
static constexpr int kScreenWidth = 320;
static constexpr int kScreenHeight = 240;

// --- Main menu 2x2 grid slot indexing ---
//   0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right

TEST(TouchTargets, MainMenuTopLeftIsSlot0) {
  EXPECT_EQ(TouchTargets::mainMenuSlot(10, 50, kScreenWidth, kScreenHeight), 0);
  EXPECT_EQ(TouchTargets::mainMenuSlot(0, STATUS_BAR_HEIGHT, kScreenWidth, kScreenHeight), 0);
}

TEST(TouchTargets, MainMenuTopRightIsSlot1) {
  EXPECT_EQ(TouchTargets::mainMenuSlot(200, 50, kScreenWidth, kScreenHeight), 1);
  EXPECT_EQ(TouchTargets::mainMenuSlot(319, STATUS_BAR_HEIGHT, kScreenWidth, kScreenHeight), 1);
}

TEST(TouchTargets, MainMenuBottomLeftIsSlot2) {
  EXPECT_EQ(TouchTargets::mainMenuSlot(10, 180, kScreenWidth, kScreenHeight), 2);
}

TEST(TouchTargets, MainMenuBottomRightIsSlot3) {
  EXPECT_EQ(TouchTargets::mainMenuSlot(200, 180, kScreenWidth, kScreenHeight), 3);
  EXPECT_EQ(TouchTargets::mainMenuSlot(319, 239, kScreenWidth, kScreenHeight), 3);
}

TEST(TouchTargets, MainMenuStatusBarTapIsRejected) {
  // Taps in the status-bar strip (y < STATUS_BAR_HEIGHT) must not map to a
  // menu slot (B8) — a negative result signals "no button".
  EXPECT_LT(TouchTargets::mainMenuSlot(10, 0, kScreenWidth, kScreenHeight), 0);
  EXPECT_LT(TouchTargets::mainMenuSlot(200, STATUS_BAR_HEIGHT - 1, kScreenWidth, kScreenHeight), 0);
  // The first row inside the content area is still slot 0/1.
  EXPECT_EQ(TouchTargets::mainMenuSlot(10, STATUS_BAR_HEIGHT, kScreenWidth, kScreenHeight), 0);
}

TEST(TouchTargets, MainMenuBoundaryBetweenLeftAndRight) {
  // x < 160 is left, x >= 160 is right
  EXPECT_EQ(TouchTargets::mainMenuSlot(159, 50, kScreenWidth, kScreenHeight), 0);
  EXPECT_EQ(TouchTargets::mainMenuSlot(160, 50, kScreenWidth, kScreenHeight), 1);
}

TEST(TouchTargets, MainMenuBoundaryBetweenTopAndBottom) {
  // contentHeight = 210, buttonHeight = 105
  // y = STATUS_BAR_HEIGHT + 104 = 134 → top, y = 135 → bottom
  EXPECT_EQ(TouchTargets::mainMenuSlot(10, 134, kScreenWidth, kScreenHeight), 0);
  EXPECT_EQ(TouchTargets::mainMenuSlot(10, 135, kScreenWidth, kScreenHeight), 2);
}

// --- Single-button hit test ---

TEST(TouchTargets, IsInsideButtonDetectsHit) {
  EXPECT_TRUE(TouchTargets::isInsideButton(30, 160, 20, 150, 280, 50));
  // Center of button
  EXPECT_TRUE(TouchTargets::isInsideButton(160, 175, 20, 150, 280, 50));
}

TEST(TouchTargets, IsInsideButtonDetectsMiss) {
  // Above
  EXPECT_FALSE(TouchTargets::isInsideButton(160, 149, 20, 150, 280, 50));
  // Below
  EXPECT_FALSE(TouchTargets::isInsideButton(160, 201, 20, 150, 280, 50));
  // Left
  EXPECT_FALSE(TouchTargets::isInsideButton(19, 175, 20, 150, 280, 50));
  // Right
  EXPECT_FALSE(TouchTargets::isInsideButton(301, 175, 20, 150, 280, 50));
}

TEST(TouchTargets, IsInsideButtonEdgesAreInclusive) {
  // Exact edges should be inside
  EXPECT_TRUE(TouchTargets::isInsideButton(20, 150, 20, 150, 280, 50));   // top-left
  EXPECT_TRUE(TouchTargets::isInsideButton(300, 200, 20, 150, 280, 50));  // bottom-right
}
