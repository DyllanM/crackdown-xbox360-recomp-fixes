#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crackdown {

inline constexpr int kFrameRateUnlimited = 0;
inline constexpr int kOriginalFrameRate = 30;
inline constexpr int kDefaultFrameRate = 60;
inline constexpr int kGuestVblankRate = 60;
inline constexpr std::array<int, 8> kFrameRates = {30, 60, 90, 120, 144, 165, 240, kFrameRateUnlimited};

// Measured frames are kept in these bounds: a longer frame slows the game down, as a missed frame did on the
// console, instead of making the next step larger than the title's own 1/30 s.
inline constexpr double kMinMeasuredFrameSeconds = 0.00025;
inline constexpr double kMaxMeasuredFrameSeconds = 1.0 / kOriginalFrameRate;

// The launcher's choices; any other value (an old or hand-edited one) is the default.
constexpr int NormalizeFrameRate(int rate) {
  return std::find(kFrameRates.begin(), kFrameRates.end(), rate) != kFrameRates.end() ? rate : kDefaultFrameRate;
}

// The saved setting as a frame rate; text that is not one of the choices is the default.
inline int ParseFrameRate(std::string_view text) {
  int rate = 0;
  const char* end = text.data() + text.size();
  const auto [stop, error] = std::from_chars(text.data(), end, rate);
  return error == std::errc() && stop == end ? NormalizeFrameRate(rate) : kDefaultFrameRate;
}

// The launcher's Frame rate choices as {value, label}.
inline std::vector<std::pair<std::string, std::string>> FrameRateOptions() {
  std::vector<std::pair<std::string, std::string>> options;
  for (const int rate : kFrameRates) {
    std::string label = rate == kFrameRateUnlimited ? "Unlimited" : std::to_string(rate) + " FPS";
    if (rate == kOriginalFrameRate) {
      label += " (Original)";
    } else if (rate == kDefaultFrameRate) {
      label += " (Recommended)";
    }
    options.emplace_back(std::to_string(rate), std::move(label));
  }
  return options;
}

// 30 and 60 wait for the 60 Hz guest vblank; higher rates and Unlimited release each swap at once and are paced on
// the host.
constexpr bool HostPacedFrameRate(int rate) {
  return rate == kFrameRateUnlimited || rate > kGuestVblankRate;
}

// Present interval in guest vblanks; 0 releases the swap at once.
constexpr uint32_t PresentIntervalFor(int rate) {
  return HostPacedFrameRate(rate) ? 0 : uint32_t(kGuestVblankRate / rate);
}

// The title sleeps until 1000 / limiter ms after the frame started while its fixed step is on.
constexpr uint32_t LimiterFrameRateFor(int rate) {
  return rate == kFrameRateUnlimited ? 1000 : uint32_t(32 * rate / kOriginalFrameRate);
}

// One frame of game time, before the title's time scale.
struct FrameStep {
  double seconds;
  float seconds_f;
  // In the title's own 1/30 s steps.
  double original_steps;
};

constexpr FrameStep FixedFrameStep(int rate) {
  return {1.0 / rate, 1.0f / float(rate), double(kOriginalFrameRate) / rate};
}

constexpr FrameStep MeasuredFrameStep(double seconds) {
  const double clamped = std::clamp(seconds, kMinMeasuredFrameSeconds, kMaxMeasuredFrameSeconds);
  return {clamped, float(clamped), clamped * kOriginalFrameRate};
}

// A step already known to be within bounds (CompensatedStepClock clamps its own result), so it is packaged without
// MeasuredFrameStep's fixed 1/30 s ceiling.
constexpr FrameStep StepFromSeconds(double seconds) {
  return {seconds, float(seconds), seconds * kOriginalFrameRate};
}

// The character's step-down snap (sub_822B4D40) ends with out.y = -(clamp(drop) * gain) / dt: it pulls the player down
// the whole drop in one step, so a stair or slope drop gives twice the downward speed at 60 FPS, which the character
// proxy turns into forward speed (the stairs and downhill speed bug). Below kGroundSnapMaxStep the velocity is scaled
// by dt * 30, i.e. divided by 1/30 instead of dt, so it covers the drop over the time it takes at 30 FPS. A 30 FPS
// frame, measured at 33 or 34 ms, keeps the title's value. Adapted from Unowa001/crackdown-recomp
// (--sim_ground_snap_30hz, BSD 3-Clause).
inline constexpr float kGroundSnapMaxStep = 1.0f / 32.0f;

constexpr float GroundSnapVelocity(float velocity, float dt) {
  return dt > 0.0f && dt < kGroundSnapMaxStep ? velocity * (dt * float(kOriginalFrameRate)) : velocity;
}

// 30 and 60 present at the guest vblank, so a slow frame is already free of host pacing; this measures how far
// behind it fell and compensates instead of letting a fixed per-call step turn the slowdown into slow motion. A
// frame close to a whole number of the rate's own steps (within the tolerance) snaps to exactly that many, so
// timer jitter does not perturb an on-time frame; a frame further off advances by the real interval, clamped to at
// most kMaxCompensatedSteps steps so one huge stall cannot become one huge physics step. The gap between the step
// taken and the real interval carries to the next frame (clamped to half a step), so sustained slow frames still
// advance game time at the real rate instead of drifting into permanent debt, and a stall past the cap drops its
// excess instead of being owed back. Adapted from Geometry Wars Retro Evolved 2's GameDeltaClock.
inline constexpr int kMaxCompensatedSteps = 3;

class CompensatedStepClock {
 public:
  enum class Kind { kSnapped, kCompensated, kClamped };

  struct Result {
    double seconds;
    Kind kind;
  };

  Result Take(double interval, double step) {
    const double target = interval + owed_;
    const double steps = target / step;
    const double nearest = std::clamp(std::round(steps), 1.0, double(kMaxCompensatedSteps));
    const double cap = kMaxCompensatedSteps * step;
    const bool snap = std::fabs(steps - nearest) <= kSnapTolerance;
    double delta = snap ? nearest * step : target;
    const bool clamped = delta > cap || delta < kMinMeasuredFrameSeconds;
    delta = std::clamp(delta, kMinMeasuredFrameSeconds, cap);
    owed_ = target > cap ? 0.0 : std::clamp(target - delta, -0.5 * step, 0.5 * step);
    return {delta, snap ? Kind::kSnapped : (clamped ? Kind::kClamped : Kind::kCompensated)};
  }

  void Reset() { owed_ = 0.0; }
  double owed() const { return owed_; }

 private:
  static constexpr double kSnapTolerance = 0.2;
  double owed_ = 0.0;
};

// Splits a running total into whole units and the fraction carried to the next frame.
constexpr uint32_t TakeWhole(double& total) {
  const uint32_t whole = uint32_t(total);
  total -= whole;
  return whole;
}

// Host pacing for HostPacedFrameRate rates. A fixed rate releases frames on a grid of its period, so sleep overshoot
// never accumulates, and steps exactly one period per frame; a frame later than a whole period restarts the grid and
// steps the time it really took. Unlimited never waits and steps the measured time.
class FramePacer {
 public:
  struct Frame {
    int64_t release_ns;  // wait until this time before running the frame
    FrameStep step;
  };

  Frame Next(int rate, int64_t now_ns) {
    if (rate != rate_ || !started_) {
      rate_ = rate;
      started_ = true;
      Restart(now_ns);
      return {now_ns, rate == kFrameRateUnlimited ? FixedFrameStep(kGuestVblankRate) : FixedFrameStep(rate)};
    }
    if (rate == kFrameRateUnlimited) {
      const int64_t elapsed = now_ns - last_ns_;
      last_ns_ = now_ns;
      return {now_ns, MeasuredFrameStep(double(elapsed) * 1e-9)};
    }
    if (++index_ == rate) {
      // A whole second of periods is exactly 1e9 ns, so the grid moves on without rounding.
      base_ns_ += kNsPerSecond;
      index_ = 0;
    }
    const int64_t deadline = base_ns_ + index_ * kNsPerSecond / rate;
    if (now_ns > deadline + kNsPerSecond / rate) {
      const int64_t elapsed = now_ns - last_ns_;
      Restart(now_ns);
      return {now_ns, MeasuredFrameStep(double(elapsed) * 1e-9)};
    }
    last_ns_ = deadline;
    return {deadline, FixedFrameStep(rate)};
  }

 private:
  static constexpr int64_t kNsPerSecond = 1'000'000'000;

  void Restart(int64_t now_ns) {
    base_ns_ = now_ns;
    last_ns_ = now_ns;
    index_ = 0;
  }

  int rate_ = kDefaultFrameRate;
  bool started_ = false;
  int64_t base_ns_ = 0;
  int64_t last_ns_ = 0;
  int64_t index_ = 0;
};

}  // namespace crackdown
