#include <cmath>
#include <cstdint>

#include "check.h"
#include "frame_timing.h"

using namespace crackdown;

namespace {

constexpr int64_t kSecond = 1'000'000'000;

void TestChoices() {
  for (const int rate : kFrameRates) {
    CHECK(NormalizeFrameRate(rate) == rate);
  }
  CHECK(NormalizeFrameRate(75) == 60);
  CHECK(NormalizeFrameRate(-1) == 60);
  CHECK(NormalizeFrameRate(1000) == 60);
  CHECK(ParseFrameRate("144") == 144);
  CHECK(ParseFrameRate("0") == kFrameRateUnlimited);
  CHECK(ParseFrameRate("") == 60);
  CHECK(ParseFrameRate("abc") == 60);
  CHECK(ParseFrameRate("120fps") == 60);
  CHECK(ParseFrameRate("59") == 60);
  const auto options = FrameRateOptions();
  CHECK(options.size() == kFrameRates.size());
  CHECK(options.front().first == "30" && options.back().first == "0" && options.back().second == "Unlimited");
}

void TestModes() {
  CHECK(!HostPacedFrameRate(30) && !HostPacedFrameRate(60));
  for (const int rate : {90, 120, 144, 165, 240, kFrameRateUnlimited}) {
    CHECK(HostPacedFrameRate(rate));
    CHECK(PresentIntervalFor(rate) == 0);
  }
  CHECK(PresentIntervalFor(30) == 2);
  CHECK(PresentIntervalFor(60) == 1);
  // The shipped limiter and the one the proven 60 FPS mode uses.
  CHECK(LimiterFrameRateFor(30) == 32);
  CHECK(LimiterFrameRateFor(60) == 64);
  for (const int rate : kFrameRates) {
    CHECK(LimiterFrameRateFor(rate) > 0 && 1000 / LimiterFrameRateFor(rate) <= 1000 / 32);
  }
}

void TestFixedSteps() {
  // 30 is the shipped step; 60 is the arithmetic of the tested 60 FPS mode.
  CHECK(FixedFrameStep(30).original_steps == 1.0);
  CHECK(FixedFrameStep(60).original_steps == 0.5);
  CHECK(FixedFrameStep(60).seconds_f == 1.0f / 60.0f);
  CHECK(FixedFrameStep(120).original_steps == 0.25);
  for (const int rate : kFrameRates) {
    if (rate) {
      CHECK(std::fabs(FixedFrameStep(rate).seconds * rate - 1.0) < 1e-12);
    }
  }
}

void TestMeasuredBounds() {
  CHECK(MeasuredFrameStep(0.0).seconds == kMinMeasuredFrameSeconds);
  CHECK(MeasuredFrameStep(-1.0).seconds == kMinMeasuredFrameSeconds);
  CHECK(MeasuredFrameStep(5.0).seconds == kMaxMeasuredFrameSeconds);
  CHECK(MeasuredFrameStep(5.0).original_steps == 1.0);
  CHECK(MeasuredFrameStep(0.004).seconds == 0.004);
}

// Logical ticks advance at 30 Hz whatever the rate, without drift.
void TestLogicalTicks() {
  for (const int rate : {60, 90, 120, 144, 165, 240}) {
    double fraction = 0.0;
    uint64_t ticks = 0;
    const int seconds = 3600;
    for (int64_t i = 0; i < int64_t(rate) * seconds; ++i) {
      fraction += FixedFrameStep(rate).original_steps;
      ticks += TakeWhole(fraction);
    }
    CHECK(ticks + 1 >= uint64_t(30) * seconds && ticks <= uint64_t(30) * seconds);
    CHECK(fraction >= 0.0 && fraction < 1.0);
  }
  double fraction = 0.0;
  CHECK(TakeWhole(fraction) == 0);
  fraction = 2.75;
  CHECK(TakeWhole(fraction) == 2 && fraction == 0.75);
}

// A frame that is always on time: released on the grid, one period of game time each.
void TestPacerOnTime() {
  for (const int rate : {90, 120, 144, 165, 240}) {
    FramePacer pacer;
    int64_t now = 5 * kSecond;
    FramePacer::Frame frame = pacer.Next(rate, now);
    CHECK(frame.release_ns == now);
    const int64_t start = now;
    double game = 0.0;
    for (int i = 1; i <= rate * 10; ++i) {
      now += 1'000'000;  // the frame's own work, shorter than any period here
      frame = pacer.Next(rate, now);
      CHECK(frame.release_ns >= now);
      CHECK(frame.step.seconds == 1.0 / rate);
      game += frame.step.seconds;
      now = frame.release_ns + 50'000;  // wake-up overshoot
    }
    // The grid does not drift with overshoot: 10 s of frames end 10 s after the start.
    CHECK(frame.release_ns == start + 10 * kSecond);
    CHECK(std::fabs(game - 10.0) < 1e-9);
  }
}

// Late frames: a little late keeps the grid; more than a period restarts it and steps the measured time.
void TestPacerLate() {
  FramePacer pacer;
  int64_t now = kSecond;
  pacer.Next(120, now);
  const int64_t period = kSecond / 120;
  FramePacer::Frame frame = pacer.Next(120, now + period + period / 2);
  CHECK(frame.release_ns == now + period);
  CHECK(frame.step.seconds == 1.0 / 120);
  const int64_t late = now + 2 * period + 3 * period;
  frame = pacer.Next(120, late);
  CHECK(frame.release_ns == late);
  CHECK(std::fabs(frame.step.seconds - double(late - (now + period)) * 1e-9) < 1e-12);
  frame = pacer.Next(120, late + 1000);
  CHECK(frame.release_ns == late + period);
  // A stall is capped at the title's own 1/30 s step.
  frame = pacer.Next(120, late + 10 * kSecond);
  CHECK(frame.step.seconds == kMaxMeasuredFrameSeconds);
}

void TestPacerUnlimited() {
  FramePacer pacer;
  FramePacer::Frame frame = pacer.Next(kFrameRateUnlimited, kSecond);
  CHECK(frame.release_ns == kSecond);
  CHECK(frame.step.seconds > 0.0);
  frame = pacer.Next(kFrameRateUnlimited, kSecond + 4'000'000);
  CHECK(frame.release_ns == kSecond + 4'000'000);
  CHECK(std::fabs(frame.step.seconds - 0.004) < 1e-12);
  frame = pacer.Next(kFrameRateUnlimited, kSecond + 4'000'000);
  CHECK(frame.step.seconds == kMinMeasuredFrameSeconds);
  frame = pacer.Next(kFrameRateUnlimited, 3 * kSecond);
  CHECK(frame.step.seconds == kMaxMeasuredFrameSeconds);
  // Switching to a fixed rate starts a new grid.
  frame = pacer.Next(144, 4 * kSecond);
  CHECK(frame.release_ns == 4 * kSecond && frame.step.seconds == 1.0 / 144);
}

// The published frame milliseconds: whole, never 0 (the camera divides by its time step), and exact over time.
void TestFrameMs() {
  // 60 FPS: 16 or 17 ms a frame, 1000 ms a second.
  double total = 0.0;
  uint32_t sum = 0;
  for (int i = 0; i < 60; ++i) {
    total += FixedFrameStep(60).seconds * 1000.0;
    const uint32_t ms = TakeFrameMs(total);
    CHECK(ms == 16 || ms == 17);
    sum += ms;
  }
  CHECK(sum == 1000 || sum == 999);
  // Sub-millisecond frames (Unlimited) still publish at least 1 ms and pay it back afterwards.
  total = 0.0;
  sum = 0;
  for (int i = 0; i < 4000; ++i) {
    total += 0.25;
    const uint32_t ms = TakeFrameMs(total);
    CHECK(ms >= 1);
    sum += ms;
  }
  CHECK(sum >= 1000 && sum <= 4000);
  CHECK(std::fabs(double(sum) + total - 1000.0) < 1e-6);
  // A 30 FPS step is 33 or 34.
  total = 0.0;
  for (int i = 0; i < 30; ++i) {
    total += FixedFrameStep(30).seconds * 1000.0;
    const uint32_t ms = TakeFrameMs(total);
    CHECK(ms == 33 || ms == 34);
  }
}

// The snap's velocity is drop * gain / dt; the hook makes it drop * gain * 30 for steps shorter than a 30 FPS frame.
void TestGroundSnap() {
  constexpr float kDropTimesGain = 0.25f;
  auto title_velocity = [](float dt) { return kDropTimesGain / dt; };
  for (const int rate : {60, 90, 120, 144, 165, 240}) {
    const float dt = FixedFrameStep(rate).seconds_f;
    CHECK(std::fabs(GroundSnapVelocity(title_velocity(dt), dt) - kDropTimesGain * 30.0f) < 1e-4f);
  }
  // 30 FPS, as fixed and as measured (33 or 34 ms), and a missed 60 FPS frame stepped as 2/60, stay stock.
  for (const float dt : {FixedFrameStep(30).seconds_f, 0.033f, 0.034f, 2.0f / 60.0f, 0.05f}) {
    CHECK(GroundSnapVelocity(title_velocity(dt), dt) == title_velocity(dt));
  }
  // A paused or degenerate step is left alone.
  CHECK(GroundSnapVelocity(5.0f, 0.0f) == 5.0f);
  CHECK(GroundSnapVelocity(5.0f, -0.01f) == 5.0f);
  // No snap this step.
  CHECK(GroundSnapVelocity(0.0f, 1.0f / 60) == 0.0f);
}

}  // namespace

int main() {
  TestChoices();
  TestModes();
  TestFixedSteps();
  TestMeasuredBounds();
  TestLogicalTicks();
  TestPacerOnTime();
  TestPacerLate();
  TestPacerUnlimited();
  TestGroundSnap();
  TestFrameMs();
  return check::Finish("frame_timing_tests");
}
