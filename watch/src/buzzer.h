// Audible fault alerts (issue #162) on the PCM amplifier. The haptic is the
// alert you feel; this is the one you hear across the cockpit when the watch
// is under a sleeve or the wrist is busy on the tiller.
//
// Tones play on their own FreeRTOS task, the one place this firmware needs
// one: LilyGoLib's Player::write() blocks on the I2S DMA queue until the
// samples are consumed, so synthesising a two-second pattern on the loop
// task would stall LVGL, the button poll and the ESP-NOW handoff for two
// seconds. Only I2S touches the task; the speaker rail is switched over I2C
// from tick(), on the loop task, because the same bus carries the PMU, the
// touch controller and the haptic driver and nothing arbitrates it.
#pragma once

namespace buzzer {

// Faults only. Recovery is deliberately felt and not heard — a fault has to
// interrupt you, but good news arriving unbidden doesn't need announcing to
// everyone aboard.
enum class Pattern : unsigned char {
  LINK_LOST,  // three long tones
  GPS_LOST,   // warbling two-tone — the ear's version of the rough buzz
};

// Starts the audio task and parks the speaker rail off. Call after
// instance.begin(), which installs the I2S driver.
void begin();

// Queue a pattern. Fire-and-forget: returns immediately, plays on the task.
void play(Pattern pattern);

// Call from the UI tick (loop task). Powers the speaker rail down once the
// last queued pattern has finished — the amplifier's quiescent draw is small
// but this firmware is already switching the GNSS off for the same reason.
void tick();

}  // namespace buzzer
