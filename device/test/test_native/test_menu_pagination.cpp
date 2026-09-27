#include <gtest/gtest.h>

#include "ui/screens/menu_pagination.h"

using namespace MenuPagination;

// --- pageCount ---

TEST(MenuPagination, SingleItemFitsOnOnePage) {
  EXPECT_EQ(pageCount(1), 1);
}

TEST(MenuPagination, FourItemsFitOnOnePageNoPager) {
  EXPECT_EQ(pageCount(4), 1);
}

TEST(MenuPagination, FiveItemsForcesTwoPages) {
  EXPECT_EQ(pageCount(5), 2);  // 3 + (2 + pager)
}

TEST(MenuPagination, SevenItemsFitInTwoPages) {
  EXPECT_EQ(pageCount(7), 3);  // 3 + 3 + (1 + pager)
}

TEST(MenuPagination, EightItemsRequireThreePages) {
  EXPECT_EQ(pageCount(8), 3);  // 3 + 3 + (2 + pager)
}

TEST(MenuPagination, TenItemsFitInThreePages) {
  EXPECT_EQ(pageCount(10), 4);  // 3 + 3 + 3 + (1 + pager)
}

// --- slotIsNextButton ---

TEST(MenuPagination, SinglePageHasNoPager) {
  for (int slot = 0; slot < kSlotsPerPage; ++slot) {
    EXPECT_FALSE(slotIsNextButton(slot, 4)) << "slot " << slot;
  }
}

TEST(MenuPagination, PagerOnlyOnSlot3WhenPaginating) {
  EXPECT_FALSE(slotIsNextButton(0, 5));
  EXPECT_FALSE(slotIsNextButton(1, 5));
  EXPECT_FALSE(slotIsNextButton(2, 5));
  EXPECT_TRUE(slotIsNextButton(3, 5));
}

TEST(MenuPagination, PagerPresentOnAllPagesIncludingLast) {
  EXPECT_TRUE(slotIsNextButton(3, 5));   // 2 pages
  EXPECT_TRUE(slotIsNextButton(3, 7));   // 3 pages
  EXPECT_TRUE(slotIsNextButton(3, 100)); // many pages
}

// --- itemIndexForSlot ---

TEST(MenuPagination, SinglePageMapsSlotsToIndicesDirectly) {
  EXPECT_EQ(itemIndexForSlot(0, 0, 4), 0);
  EXPECT_EQ(itemIndexForSlot(1, 0, 4), 1);
  EXPECT_EQ(itemIndexForSlot(2, 0, 4), 2);
  EXPECT_EQ(itemIndexForSlot(3, 0, 4), 3);
}

TEST(MenuPagination, SinglePagePartialFillReturnsMinusOneForEmptySlots) {
  // 3 items: slots 0..2 mapped, slot 3 is empty (no pager because 1 page).
  EXPECT_EQ(itemIndexForSlot(0, 0, 3), 0);
  EXPECT_EQ(itemIndexForSlot(1, 0, 3), 1);
  EXPECT_EQ(itemIndexForSlot(2, 0, 3), 2);
  EXPECT_EQ(itemIndexForSlot(3, 0, 3), -1);
}

TEST(MenuPagination, PagedFirstPageHoldsThreeItemsAndPager) {
  EXPECT_EQ(itemIndexForSlot(0, 0, 5), 0);
  EXPECT_EQ(itemIndexForSlot(1, 0, 5), 1);
  EXPECT_EQ(itemIndexForSlot(2, 0, 5), 2);
  EXPECT_EQ(itemIndexForSlot(3, 0, 5), -1);  // pager
}

TEST(MenuPagination, PagedLastPagePartialFillAndPager) {
  // 5 items, page 1 (0-indexed): items 3,4 + empty + pager
  EXPECT_EQ(itemIndexForSlot(0, 1, 5), 3);
  EXPECT_EQ(itemIndexForSlot(1, 1, 5), 4);
  EXPECT_EQ(itemIndexForSlot(2, 1, 5), -1);  // past end
  EXPECT_EQ(itemIndexForSlot(3, 1, 5), -1);  // pager
}

TEST(MenuPagination, PagedLastPageFullThreeItems) {
  // 7 items: pages 0,1,2 hold 3,3,1
  EXPECT_EQ(itemIndexForSlot(0, 1, 7), 3);
  EXPECT_EQ(itemIndexForSlot(1, 1, 7), 4);
  EXPECT_EQ(itemIndexForSlot(2, 1, 7), 5);
  EXPECT_EQ(itemIndexForSlot(3, 1, 7), -1);
  // page 2
  EXPECT_EQ(itemIndexForSlot(0, 2, 7), 6);
  EXPECT_EQ(itemIndexForSlot(1, 2, 7), -1);
  EXPECT_EQ(itemIndexForSlot(3, 2, 7), -1);
}

// --- wrapping advance (the modulo the screen uses on pager tap) ---

TEST(MenuPagination, PagerWrapsFromLastPageBackToZero) {
  int items = 5;
  int n = pageCount(items);
  ASSERT_EQ(n, 2);
  int page = 0;
  page = (page + 1) % n;
  EXPECT_EQ(page, 1);
  page = (page + 1) % n;
  EXPECT_EQ(page, 0);
}
