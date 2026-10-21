#include <algorithm>
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

// A late frame restarts the grid at once and steps more than a period, but less than the raw interval.
void TestPacerLate() {
  FramePacer pacer;
  int64_t now = kSecond;
  pacer.Next(120, now);
  const int64_t period = kSecond / 120;
  const int64_t late = now + period + period / 2;
  FramePacer::Frame frame = pacer.Next(120, late);
  CHECK(frame.release_ns == late);
  CHECK(frame.step.seconds > 1.0 / 120 && frame.step.seconds < 1.5 / 120);
  // The next slot is one period after the late frame.
  frame = pacer.Next(120, late + 1000);
  CHECK(frame.release_ns == late + period);
  // A stall is capped at the title's own 1/30 s step and is not owed back.
  frame = pacer.Next(120, late + 10 * kSecond);
  CHECK(frame.step.seconds <= kMaxMeasuredFrameSeconds);
  int64_t t = frame.release_ns;
  double game = 0.0;
  for (int i = 0; i < 120; ++i) {
    frame = pacer.Next(120, t + 1'000'000);
    t = frame.release_ns;
    game += frame.step.seconds;
  }
  // One second of on-time frames after the stall: about one second of game time, nothing near the stall's 10 s.
  CHECK(game > 0.95 && game < 1.0 + kMaxMeasuredFrameSeconds);
  CHECK(std::fabs(frame.step.seconds - 1.0 / 120) < 1e-4);
}

// The case behind the launches on slopes: the GPU cannot hold 120, so every frame is late by a varying amount. The
// steps follow the real frame time smoothly instead of alternating short fixed steps with long catch-up steps, and
// game time keeps up with real time.
void TestPacerCannotKeepUp() {
  FramePacer pacer;
  int64_t now = kSecond;
  FramePacer::Frame frame = pacer.Next(120, now);
  const int64_t start = now;
  double game = 0.0;
  double previous = 0.0;
  double largest_jump = 0.0;
  for (int i = 0; i < 600; ++i) {
    const int64_t work = (i % 3 == 0 ? 14'000'000 : 9'500'000);  // 9.5 ms, 9.5 ms, 14 ms: about 91 FPS
    now = frame.release_ns + work;
    frame = pacer.Next(120, now);
    game += frame.step.seconds;
    if (i > 30) {
      largest_jump = std::max(largest_jump, frame.step.seconds / previous);
      CHECK(frame.step.seconds > 0.008 && frame.step.seconds < 0.013);
    }
    previous = frame.step.seconds;
  }
  // No step is more than a third longer than the one before it (the old pacer alternated 8.3 ms and 17+ ms).
  CHECK(largest_jump < 1.34);
  const double real = double(frame.release_ns - start) * 1e-9;
  CHECK(std::fabs(game - real) < 2 * kMaxMeasuredFrameSeconds);
}

// The smoother alone: exact on a steady interval, follows a new steady interval, never above 1/30 s.
void TestStepSmoother() {
  StepSmoother smoother;
  smoother.Reset(1.0 / 120);
  for (int i = 0; i < 10; ++i) CHECK(smoother.Take(1.0 / 120) == 1.0 / 120);
  CHECK(smoother.owed() == 0.0);
  double step = 0.0;
  for (int i = 0; i < 60; ++i) step = smoother.Take(0.012);
  CHECK(std::fabs(step - 0.012) < 1e-6);
  CHECK(smoother.Take(5.0) <= kMaxMeasuredFrameSeconds);
  CHECK(smoother.owed() <= kMaxMeasuredFrameSeconds);
}

void TestDebugFrameDips() {
  // Off: never a delay.
  CHECK(DebugFrameDipSeconds(0, 3.0, 0.5, 0.5) == 0.0);
  // No load at the start of the sweep, full load half way, within 60-100% of it.
  CHECK(DebugFrameDipSeconds(20, 0.0, 0.9, 0.5) == 0.0);
  const double full = DebugFrameDipSeconds(20, kDipPeriodSeconds / 2, 0.0, 0.5);
  CHECK(std::fabs(full - 0.012) < 1e-9);
  CHECK(std::fabs(DebugFrameDipSeconds(20, kDipPeriodSeconds / 2, 0.999999, 0.5) - 0.02) < 1e-6);
  // A spike stalls for twice the maximum; the maximum itself is capped.
  CHECK(std::fabs(DebugFrameDipSeconds(20, 1.0, 0.5, 0.0) - 0.04) < 1e-12);
  CHECK(std::fabs(DebugFrameDipSeconds(1000, 1.0, 0.5, 0.0) - 2.0 * kMaxDebugDipMs * 1e-3) < 1e-12);
}

void TestPacerUnlimited() {
  FramePacer pacer;
  FramePacer::Frame frame = pacer.Next(kFrameRateUnlimited, kSecond);
  CHECK(frame.release_ns == kSecond);
  CHECK(frame.step.seconds > 0.0);
  // Steady 4 ms frames: the smoothed step settles on 4 ms, and game time matches real time.
  int64_t now = kSecond;
  double game = 0.0;
  for (int i = 0; i < 200; ++i) {
    now += 4'000'000;
    frame = pacer.Next(kFrameRateUnlimited, now);
    CHECK(frame.release_ns == now);
    game += frame.step.seconds;
  }
  CHECK(std::fabs(frame.step.seconds - 0.004) < 1e-6);
  CHECK(std::fabs(game - 0.8) < 0.02);
  // One 20 ms frame does not become a 20 ms step at once; the time is paid back over the next frames.
  now += 20'000'000;
  frame = pacer.Next(kFrameRateUnlimited, now);
  CHECK(frame.step.seconds > 0.004 && frame.step.seconds < 0.012);
  // A stall never steps more than the title's own 1/30 s.
  frame = pacer.Next(kFrameRateUnlimited, 3 * kSecond);
  CHECK(frame.step.seconds <= kMaxMeasuredFrameSeconds);
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
  // A long run of tiny frames keeps its debt bounded, so a slower frame recovers at once.
  total = 0.0;
  for (int i = 0; i < 10000; ++i) {
    total += 0.1;
    TakeFrameMs(total);
  }
  CHECK(total >= -kMaxFrameMsDebt);
  // A 30 FPS step is 33 or 34.
  total = 0.0;
  for (int i = 0; i < 30; ++i) {
    total += FixedFrameStep(30).seconds * 1000.0;
    const uint32_t ms = TakeFrameMs(total);
    CHECK(ms == 33 || ms == 34);
  }
}

// Frames through the pacer never publish 0 ms, at a fixed high rate, Unlimited at sub-millisecond intervals and a
// stall.
void TestPacedFrameMsNeverZero() {
  for (const int rate : {120, 240, kFrameRateUnlimited}) {
    FramePacer pacer;
    int64_t now = kSecond;
    double total = 0.0;
    for (int i = 0; i < 2000; ++i) {
      const int64_t work = rate == kFrameRateUnlimited ? 300'000 : (i % 97 == 0 ? 50'000'000 : 1'000'000);
      const FramePacer::Frame frame = pacer.Next(rate, now);
      total += frame.step.seconds * 1000.0;
      CHECK(TakeFrameMs(total) >= 1);
      now = frame.release_ns + work;
    }
  }
}

// Per-update constants keep their per-second total at 30, 60, 120 and Unlimited, and 30 stays bit-exact.
void TestPerOriginalStep() {
  constexpr float kAnimStep = 1.0f / 30.0f;
  constexpr float kTimerStep = 0.033f;
  CHECK(ScalePerOriginalStep(kAnimStep, 1.0) == kAnimStep);
  CHECK(ScalePerOriginalStep(kTimerStep, 1.0) == kTimerStep);
  for (const int rate : {30, 60, 120}) {
    const FrameStep step = FixedFrameStep(rate);
    double second = 0.0;
    for (int i = 0; i < rate; ++i) {
      second += ScalePerOriginalStep(kTimerStep, step.original_steps);
    }
    CHECK(std::fabs(second - 30.0 * kTimerStep) < 1e-4);
  }
  CHECK(std::fabs(ScalePerOriginalStep(kAnimStep, 0.1) - kAnimStep * 0.1f) < 1e-9f);
  CHECK(ScalePerOriginalStep(kAnimStep, 0.0) == kAnimStep);
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
  TestPacerCannotKeepUp();
  TestStepSmoother();
  TestPacerUnlimited();
  TestDebugFrameDips();
  TestFrameMs();
  TestPacedFrameMsNeverZero();
  TestPerOriginalStep();
  return check::Finish("frame_timing_tests");
}
