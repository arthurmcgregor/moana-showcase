#pragma once

// Pure pagination math for the 2x2 main-menu grid. No Arduino/display deps so
// it can be unit-tested on the host.
//
// Layout rules:
//   - If itemCount <= kSlotsPerPage: a single page shows every item, no pager.
//   - Otherwise: every page reserves slot 3 (bottom-right) for the ">>" pager,
//     so each page holds at most 3 items. The pager wraps from the last page
//     back to page 0.
//
// Slot numbering (0..3): 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.

namespace MenuPagination {

constexpr int kSlotsPerPage = 4;
constexpr int kItemsPerPagedPage = 3;
constexpr int kNextSlot = 3;

constexpr int pageCount(int itemCount) {
  if (itemCount <= kSlotsPerPage) return 1;
  return (itemCount + kItemsPerPagedPage - 1) / kItemsPerPagedPage;
}

constexpr bool slotIsNextButton(int slot, int itemCount) {
  return slot == kNextSlot && pageCount(itemCount) > 1;
}

// Returns the kItems index for `slot` on `page`, or -1 if the slot is the
// pager or falls past the end of the items array.
constexpr int itemIndexForSlot(int slot, int page, int itemCount) {
  if (slotIsNextButton(slot, itemCount)) return -1;
  int idx = page * kItemsPerPagedPage + slot;
  return (idx < itemCount) ? idx : -1;
}

}  // namespace MenuPagination
