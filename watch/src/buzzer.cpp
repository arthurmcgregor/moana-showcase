#include "buzzer.h"

#include <LilyGoLib.h>

#include <atomic>

namespace buzzer {
namespace {

// LilyGoLib installs the I2S driver at 44.1 kHz / 16-bit / all-right (see
// Player::init) and nothing here changes it — a square wave a few kHz up
// doesn't care about the rate, and re-clocking the peripheral under a shared
// driver is a good way to break the microphone later.
constexpr uint32_t kSampleRate = 44100;

// Square wave, not a sine: a tiny speaker under a spray hood needs the
// harmonics. ~80% of full scale leaves the amplifier some headroom.
constexpr int16_t kAmplitude = 26000;

// One tone or one silence. 2-3 kHz is where both the ear and a speaker this
// small are at their most efficient — a lower, sweeter note is the first
// thing lost under engine noise.
struct Step {
  uint16_t hz;  // 0 = silence
  uint16_t ms;
};

// Three long tones: the sound of something having gone away.
const Step kLinkLostSteps[] = {
    {2700, 320}, {0, 160}, {2700, 320}, {0, 160}, {2700, 320},
};
// A warble instead, matching the haptic's rough-against-smooth split so the
// two faults are as distinct through the ear as through the wrist.
const Step kGpsLostSteps[] = {
    {2200, 150}, {3100, 150}, {2200, 150},
    {3100, 150}, {2200, 150}, {3100, 150},
};
struct Sequence {
  const Step* steps;
  uint8_t count;
};

Sequence sequenceFor(Pattern p) {
  switch (p) {
    case Pattern::LINK_LOST:
      return {kLinkLostSteps,
              (uint8_t)(sizeof(kLinkLostSteps) / sizeof(Step))};
    case Pattern::GPS_LOST:
    default:
      return {kGpsLostSteps, (uint8_t)(sizeof(kGpsLostSteps) / sizeof(Step))};
  }
}

QueueHandle_t queue = nullptr;

// Patterns queued but not yet finished. Incremented on the loop task,
// decremented on the audio task, so tick() only cuts the rail once the
// speaker has genuinely stopped.
std::atomic<int> outstanding{0};
bool railOn = false;

void playStep(const Step& step) {
  static int16_t buf[256];
  uint32_t remaining = (uint32_t)kSampleRate * step.ms / 1000;
  // Half a period in samples; the wave flips level every time it elapses.
  uint32_t halfPeriod = step.hz ? kSampleRate / (2u * step.hz) : 0;
  uint32_t phase = 0;
  int16_t level = kAmplitude;

  while (remaining > 0) {
    size_t n = remaining < 256 ? remaining : 256;
    for (size_t i = 0; i < n; i++) {
      if (halfPeriod == 0) {
        buf[i] = 0;
        continue;
      }
      buf[i] = level;
      if (++phase >= halfPeriod) {
        phase = 0;
        level = (int16_t)-level;
      }
    }
    instance.player.write(buf, n * sizeof(int16_t));
    remaining -= n;
  }
}

void audioTask(void*) {
  for (;;) {
    Pattern p;
    if (xQueueReceive(queue, &p, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    Sequence seq = sequenceFor(p);
    for (uint8_t i = 0; i < seq.count; i++) {
      playStep(seq.steps[i]);
    }
    // Land on silence: the last edge would otherwise sit on the amplifier as
    // DC until the rail drops, which is audible as a click.
    playStep({0, 20});
    outstanding--;
  }
}

}  // namespace

void begin() {
  // LilyGoLib powers the speaker rail up in begin(); park it off until
  // something actually needs to make a noise.
  instance.powerControl(POWER_SPEAK, false);
  railOn = false;

  queue = xQueueCreate(4, sizeof(Pattern));
  xTaskCreatePinnedToCore(audioTask, "buzzer", 3072, nullptr, 1, nullptr, 0);
}

void play(Pattern pattern) {
  if (queue == nullptr) {
    return;
  }
  // Rail first, and from this task: the audio task must not touch I2C.
  // Idempotent, so overlapping alerts are harmless.
  instance.powerControl(POWER_SPEAK, true);
  railOn = true;
  outstanding++;
  if (xQueueSend(queue, &pattern, 0) != pdTRUE) {
    outstanding--;  // queue full — drop the tone rather than block the tick
  }
}

void tick() {
  if (railOn && outstanding.load() == 0) {
    instance.powerControl(POWER_SPEAK, false);
    railOn = false;
  }
}

}  // namespace buzzer
