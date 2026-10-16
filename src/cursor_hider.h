#pragma once

#include <chrono>
#include <optional>

namespace crackdown {

inline constexpr std::chrono::milliseconds kCursorHideDelay{5000};

// Hides the cursor in fullscreen gameplay with focus after kCursorHideDelay without mouse motion.
class CursorHider {
 public:
  using Clock = std::chrono::steady_clock;

  bool visible() const { return visible_; }

  void MouseMoved(Clock::time_point now) { last_motion_ = now; }

  // Returns the new visibility when it changes, otherwise nullopt.
  std::optional<bool> Update(bool gameplay, bool fullscreen, bool focused, Clock::time_point now) {
    const bool active = gameplay && fullscreen && focused;
    if (!active || !active_) {
      last_motion_ = now;
    }
    active_ = active;
    const bool visible = !active || now - last_motion_ < kCursorHideDelay;
    if (visible == visible_) {
      return std::nullopt;
    }
    visible_ = visible;
    return visible;
  }

 private:
  Clock::time_point last_motion_{};
  bool active_ = false;
  bool visible_ = true;
};

}  // namespace crackdown
