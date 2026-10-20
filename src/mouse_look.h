#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rex::ui {
class Window;
}

namespace crackdown::mouse_look {

// 0.020 degrees per count at 1x: a 360 turn is about 18000 counts, roughly 28 cm
// on a 1600 dpi mouse.
inline constexpr double kRadiansPerCount = 0.00035;
inline constexpr double kDefaultSensitivity = 1.0;
inline constexpr double kMinSensitivity = 0.10;
inline constexpr double kMaxSensitivity = 10.0;
inline constexpr double kMaxCounts = 1.0e6;

struct Delta {
  double x = 0.0;
  double y = 0.0;
};

struct Turn {
  double yaw = 0.0;
  double pitch = 0.0;

  bool any() const { return yaw != 0.0 || pitch != 0.0; }
};

// Mouse movement between camera updates. Callers synchronise access; the
// accumulator itself is a plain value.
class Accumulator {
 public:
  void Add(double dx, double dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
      return;
    }
    x_ = std::clamp(x_ + dx, -kMaxCounts, kMaxCounts);
    y_ = std::clamp(y_ + dy, -kMaxCounts, kMaxCounts);
  }

  Delta Take() {
    const Delta delta{x_, y_};
    Clear();
    return delta;
  }

  void Clear() { x_ = y_ = 0.0; }

  bool empty() const { return x_ == 0.0 && y_ == 0.0; }

 private:
  double x_ = 0.0;
  double y_ = 0.0;
};

inline double ClampSensitivity(double value) {
  if (!std::isfinite(value) || value <= 0.0) {
    return kDefaultSensitivity;
  }
  return std::clamp(value, kMinSensitivity, kMaxSensitivity);
}

// dx/dy are physical pixels; dpi_scale converts them back to mouse counts.
inline Turn TurnFor(const Delta& delta, double sensitivity, double dpi_scale, bool invert_y) {
  if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || dpi_scale <= 0.0 ||
      !std::isfinite(dpi_scale)) {
    return {};
  }
  if (delta.x == 0.0 && delta.y == 0.0) {
    return {};
  }
  const double scale = kRadiansPerCount * ClampSensitivity(sensitivity) / dpi_scale;
  Turn turn;
  // Crackdown's yaw accumulator decreases when the camera turns right.
  turn.yaw = -std::clamp(delta.x, -kMaxCounts, kMaxCounts) * scale;
  turn.pitch = -std::clamp(delta.y, -kMaxCounts, kMaxCounts) * scale;
  if (invert_y) {
    turn.pitch = -turn.pitch;
  }
  if (!std::isfinite(turn.yaw) || !std::isfinite(turn.pitch)) {
    return {};
  }
  return turn;
}

inline bool CanAcquireCapture(bool direct_enabled, bool focused, bool blocked) {
  return direct_enabled && focused && !blocked;
}

inline bool DirectLookAllowed(bool direct_enabled, bool focused, bool blocked, bool captured) {
  return CanAcquireCapture(direct_enabled, focused, blocked) && captured;
}

// Installs the window listener and enables the camera hooks. Safe to call once
// the window exists.
void Install(rex::ui::Window* window);
void Uninstall();

// Applies the current keyboard/mouse settings to the runtime. Call after the
// launcher's settings reach the cvars.
void Configure();

// Suspends direct look while a menu, overlay or ImGui capture owns the mouse.
void SetBlocked(bool blocked);

// Capture ownership. A click inside gameplay requests capture; controller,
// overlay, focus loss and menu keys release it.
void SetCaptured(bool captured);
void ReleaseCapture();

// Camera hooks: adds the accumulated mouse turn to the game's new yaw/pitch
// (radians) for this player-camera update. Returns false when the registers
// should stay untouched.
bool ApplyTurn(float& yaw, float& pitch);

}  // namespace crackdown::mouse_look
