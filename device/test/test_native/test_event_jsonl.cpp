#include <gtest/gtest.h>

#include <string>

#include "recording/event_jsonl.h"

// Sidecar line formatting (issue #139). The line shape is a contract with
// the server parser (server/moana/tracks/events_jsonl.py).

TEST(EventJsonl, FormatsModeChange) {
  char buf[64];
  size_t len = EventJsonl::formatLine(buf, sizeof(buf),
                                      moana::EVENT_MODE_CHANGE,
                                      moana::MODE_SAIL, 1753900000);
  EXPECT_EQ(std::string(buf, len), "{\"kind\":\"SAIL\",\"utc\":1753900000}\n");

  // utc 0 is the "watch had no time reference" sentinel and still gets a line
  // — the server decides what to do with it.
  len = EventJsonl::formatLine(buf, sizeof(buf), moana::EVENT_MODE_CHANGE,
                               moana::MODE_MOTOR, 0);
  EXPECT_EQ(std::string(buf, len), "{\"kind\":\"MOTOR\",\"utc\":0}\n");
}

TEST(EventJsonl, WritesTheWholeModeVocabulary) {
  // Every server Mode.MODE_CHOICES value is settable from the watch picker
  // (issue #146), so every one has to reach the sidecar by name.
  struct Case {
    uint8_t mode;
    const char* name;
  };
  const Case kCases[] = {
      {moana::MODE_SAIL, "SAIL"},           {moana::MODE_MOTORSAIL, "MOTORSAIL"},
      {moana::MODE_MOTOR, "MOTOR"},         {moana::MODE_ROW, "ROW"},
      {moana::MODE_UNPOWERED, "UNPOWERED"}, {moana::MODE_ANCHOR, "ANCHOR"},
      {moana::MODE_ALONGSIDE, "ALONGSIDE"}, {moana::MODE_BEACHED, "BEACHED"},
      {moana::MODE_TRAILERED, "TRAILERED"}, {moana::MODE_OTHER, "OTHER"},
  };

  char buf[64];
  for (const Case& c : kCases) {
    size_t len = EventJsonl::formatLine(buf, sizeof(buf),
                                        moana::EVENT_MODE_CHANGE, c.mode, 1);
    EXPECT_EQ(std::string(buf, len),
              std::string("{\"kind\":\"") + c.name + "\",\"utc\":1}\n");
  }
}

TEST(EventJsonl, NonModeKindsWriteTheirOwnName) {
  char buf[64];
  size_t len = EventJsonl::formatLine(buf, sizeof(buf),
                                      moana::EVENT_VOICE_MARKER,
                                      moana::MODE_UNKNOWN, 7);
  EXPECT_EQ(std::string(buf, len), "{\"kind\":\"VOICE\",\"utc\":7}\n");

  // A kind (or mode) from a newer watch firmware still produces a parseable
  // line the server can warn about, rather than garbage.
  len = EventJsonl::formatLine(buf, sizeof(buf), 99, moana::MODE_UNKNOWN, 7);
  EXPECT_EQ(std::string(buf, len), "{\"kind\":\"UNKNOWN\",\"utc\":7}\n");
  len = EventJsonl::formatLine(buf, sizeof(buf), moana::EVENT_MODE_CHANGE, 99,
                               7);
  EXPECT_EQ(std::string(buf, len), "{\"kind\":\"UNKNOWN\",\"utc\":7}\n");
}

TEST(EventJsonl, ReturnsZeroWhenBufferTooSmall) {
  char buf[8];
  EXPECT_EQ(EventJsonl::formatLine(buf, sizeof(buf), moana::EVENT_MODE_CHANGE,
                                   moana::MODE_SAIL, 1753900000),
            0u);
}
