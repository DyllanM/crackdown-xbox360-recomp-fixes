#include <cmath>

#include "check.h"
#include "frame_timing.h"

using namespace crackdown;

namespace {

// Runs `frames` calls of a fixed `interval` and checks the simulated total tracks the wall-clock total: the clock's
// own carried difference is bounded to half a nominal step, so the gap can never grow with the number of frames.
void CheckTracksWallClock(double interval, double step, int frames) {
  CompensatedStepClock clock;
  double total = 0.0;
  for (int i = 0; i < frames; ++i) {
    total += clock.Take(interval, step).seconds;
  }
  CHECK(std::fabs(total - interval * frames) <= 0.5 * step + 1e-9);
}

void Test60RepeatedIntervals() {
  constexpr double kStep = 1.0 / 60.0;
  CheckTracksWallClock(1.0 / 60.0, kStep, 600);   // on time
  CheckTracksWallClock(0.020, kStep, 50);         // 50 FPS
  CheckTracksWallClock(0.025, kStep, 40);         // 40 FPS
  CheckTracksWallClock(1.0 / 30.0, kStep, 30);    // 30 FPS, two nominal steps each
}

void Test60MixedIntervals() {
  constexpr double kStep = 1.0 / 60.0;
  CompensatedStepClock clock;
  double total = 0.0;
  double wall = 0.0;
  const double pattern[] = {1.0 / 60.0, 1.0 / 60.0, 1.0 / 30.0};
  for (int i = 0; i < 3000; ++i) {
    const double interval = pattern[i % 3];
    total += clock.Take(interval, kStep).seconds;
    wall += interval;
    // The gap never grows with the frame count, at any point in the run.
    CHECK(std::fabs(total - wall) <= 0.5 * kStep + 1e-9);
  }
}

void Test30RepeatedIntervals() {
  constexpr double kStep = 1.0 / 30.0;
  CheckTracksWallClock(1.0 / 30.0, kStep, 300);  // on time
  CheckTracksWallClock(0.040, kStep, 75);        // ~25 FPS
  CheckTracksWallClock(0.050, kStep, 60);        // ~20 FPS
}

void Test30MixedIntervals() {
  constexpr double kStep = 1.0 / 30.0;
  CompensatedStepClock clock;
  double total = 0.0;
  double wall = 0.0;
  const double pattern[] = {1.0 / 30.0, 1.0 / 30.0, 0.050, 1.0 / 30.0, 0.040};
  for (int i = 0; i < 2000; ++i) {
    const double interval = pattern[i % 5];
    total += clock.Take(interval, kStep).seconds;
    wall += interval;
    CHECK(std::fabs(total - wall) <= 0.5 * kStep + 1e-9);
  }
}

// An on-time frame at either console rate snaps to exactly the nominal step: bit-for-bit the title's own fixed
// step, with no carried difference left behind.
void TestOnTimeSnapsExactly() {
  for (const double step : {1.0 / 30.0, 1.0 / 60.0}) {
    CompensatedStepClock clock;
    for (int i = 0; i < 1000; ++i) {
      const CompensatedStepClock::Result result = clock.Take(step, step);
      CHECK(result.kind == CompensatedStepClock::Kind::kSnapped);
      CHECK(result.seconds == step);
    }
    CHECK(clock.owed() == 0.0);
  }
}

// A huge stall is capped at kMaxCompensatedSteps nominal steps and leaves no permanent catch-up debt, so the next
// frame is free to track wall-clock time again instead of racing to pay back the dropped excess.
void TestHugeStallClampsAndRecovers() {
  constexpr double kStep = 1.0 / 30.0;
  CompensatedStepClock clock;
  const CompensatedStepClock::Result stall = clock.Take(10.0, kStep);
  CHECK(stall.kind == CompensatedStepClock::Kind::kClamped);
  CHECK(stall.seconds == double(kMaxCompensatedSteps) * kStep);
  CHECK(clock.owed() == 0.0);
  const CompensatedStepClock::Result recovered = clock.Take(kStep, kStep);
  CHECK(recovered.kind == CompensatedStepClock::Kind::kSnapped);
  CHECK(recovered.seconds == kStep);
}

}  // namespace

int main() {
  Test60RepeatedIntervals();
  Test60MixedIntervals();
  Test30RepeatedIntervals();
  Test30MixedIntervals();
  TestOnTimeSnapsExactly();
  TestHugeStallClampsAndRecovers();
  return check::Finish("compensated_step_clock_tests");
}
