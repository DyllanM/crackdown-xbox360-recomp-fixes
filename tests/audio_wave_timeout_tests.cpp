#include <cmath>
#include <cstdint>

#include "audio_wave_timeout.h"
#include "check.h"
#include "frame_timing.h"

using namespace crackdown;

namespace {

// A 5.28 s clip: the title sets the deadline to floor(samples / 48000) * 30 + 70 ticks and stops it past that.
constexpr uint32_t kDeadline = 5 * 30 + 70;

void TestUpdatesPerTick() {
  for (const int updates_per_tick : {1, 2, 4, 8}) {
    const int rate = kOriginalFrameRate * updates_per_tick;
    WaveTimeoutClock clock;
    for (int update = 1; update <= 10 * updates_per_tick; ++update) {
      const uint32_t ticks = clock.Tick(1, FixedFrameStep(rate).original_steps);
      CHECK(ticks == (update % updates_per_tick == 0 ? 1u : 0u));
    }
  }
}

void TestNoDriftAtAnyRate() {
  for (const int rate : {30, 60, 90, 120, 144, 165, 240}) {
    WaveTimeoutClock clock;
    uint64_t ticks = 0;
    const int seconds = 600;
    for (int64_t i = 0; i < int64_t(rate) * seconds; ++i) {
      ticks += clock.Tick(1, FixedFrameStep(rate).original_steps);
    }
    CHECK(ticks + 1 >= uint64_t(30) * seconds && ticks <= uint64_t(30) * seconds);
  }
}

// The timeout fires after the same game time at every rate; one tick per update fired it after 3.7 s at 60 FPS.
void TestDeadlineInGameTime() {
  for (const int rate : {30, 60, 90, 120, 144, 165, 240}) {
    WaveTimeoutClock clock;
    uint32_t elapsed = 0;
    int updates = 0;
    while (elapsed <= kDeadline) {
      elapsed += clock.Tick(1, FixedFrameStep(rate).original_steps);
      ++updates;
    }
    const double seconds = double(updates) / rate;
    CHECK(std::fabs(seconds - double(kDeadline + 1) / kOriginalFrameRate) <= 1.0 / rate + 1e-9);
  }
}

void TestMeasuredSteps() {
  WaveTimeoutClock clock;
  uint32_t ticks = 0;
  for (int i = 0; i < 250; ++i) {
    ticks += clock.Tick(1, MeasuredFrameStep(0.004).original_steps);
  }
  CHECK(ticks == 30 || ticks == 29);
  // A stalled frame advances one shipped step, never more.
  CHECK(clock.Tick(2, MeasuredFrameStep(5.0).original_steps) == 1);
}

void TestInstanceReuse() {
  WaveTimeoutClock clock;
  CHECK(clock.Tick(7, 0.5) == 0);
  clock.Reset(7);
  CHECK(clock.Tick(7, 0.5) == 0);
  CHECK(clock.Tick(7, 0.5) == 1);

  CHECK(clock.Tick(8, 0.5) == 0);
  CHECK(clock.Tick(9, 0.5) == 0);
  clock.Reset(8);
  CHECK(clock.Tick(9, 0.5) == 1);
}

}  // namespace

int main() {
  TestUpdatesPerTick();
  TestNoDriftAtAnyRate();
  TestDeadlineInGameTime();
  TestMeasuredSteps();
  TestInstanceReuse();
  return check::Finish("audio_wave_timeout_tests");
}
