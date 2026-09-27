// T-Watch Ultra speed readout — single-purpose sailing display (issue #133).
// Shows SOG from the CoreS3 boat unit's 1Hz ESP-NOW telemetry, with a
// time/battery/link header. Screen blanks after 60s, wakes on touch.
#include <LilyGoLib.h>
#include <LV_Helper.h>

#include "buzzer.h"
#include "espnow_link.h"
#include "inputs.h"
#include "ui.h"

void setup() {
  // 80 MHz is plenty for ESP-NOW RX plus a mostly-static screen, and saves
  // ~30-40 mA — the margin that turns a 6 h sail into a comfortable fit on
  // the 1100 mAh cell. Set before Serial.begin so the baud is stable.
  setCpuFrequencyMhz(80);
  Serial.begin(115200);

  instance.begin();
  beginLvglHelper(instance);

  // Speed comes from the boat unit only — no on-watch GNSS fallback, so the
  // receiver is dead weight (~25 mA). Keep it off.
  instance.powerControl(POWER_GPS, false);

  // Audible fault alerts (#162). Also parks the speaker rail off, which
  // LilyGoLib leaves powered — same reasoning as the GNSS above.
  buzzer::begin();

  espnow_link::begin();
  ui::begin();

  instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
  Serial.println("Moana watch speed readout ready");
}

void loop() {
  instance.loop();  // PMU/device event dispatch
  // Button classifier needs ~5 ms polling (30 ms debounce, 300 ms double-
  // click gap) — the 250 ms UI tick is far too coarse, so it lives here.
  inputs::poll();
  lv_timer_handler();
  delay(5);
}
