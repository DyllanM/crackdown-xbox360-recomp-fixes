#pragma once

#include <cstdint>
#include <cstdlib>
#include <optional>

#include <rex/input/state_merge.h>

namespace crackdown {

enum class CursorInputDevice { kMouse, kKeyboard, kController };

// The last meaningful input device decides the cursor; losing focus shows it.
class CursorHider {
 public:
  bool visible() const { return visible_; }
  bool overlay_open() const { return overlay_open_; }
  CursorInputDevice last_device() const { return last_device_; }

  std::optional<bool> MouseMoved() { return Note(CursorInputDevice::kMouse); }
  std::optional<bool> MouseButtonPressed() { return Note(CursorInputDevice::kMouse); }
  std::optional<bool> MouseWheelMoved() { return Note(CursorInputDevice::kMouse); }
  std::optional<bool> KeyboardInput() { return Note(CursorInputDevice::kKeyboard); }
  std::optional<bool> ControllerInput() { return Note(CursorInputDevice::kController); }

  std::optional<bool> SetOverlayOpen(bool open) {
    overlay_open_ = open;
    return Recalculate();
  }

  std::optional<bool> SetFocused(bool focused) {
    focused_ = focused;
    return Recalculate();
  }

 private:
  std::optional<bool> Note(CursorInputDevice device) {
    if (focused_) {
      last_device_ = device;
    }
    return Recalculate();
  }

  std::optional<bool> Recalculate() {
    const bool visible = !focused_ || last_device_ == CursorInputDevice::kMouse;
    if (visible == visible_) {
      return std::nullopt;
    }
    visible_ = visible;
    return visible;
  }

  CursorInputDevice last_device_ = CursorInputDevice::kMouse;
  bool overlay_open_ = false;
  bool focused_ = true;
  bool visible_ = true;
};

// Mouse look owns capture and hiding while it is active, and overlays or ImGui capture suspend that ownership.
inline bool MouseLookOwnsCursor(bool focused, bool overlay_open, bool ui_wants_mouse, bool mnk_mode,
                                bool mnk_mouse, bool mouse_look_active) {
  return focused && !overlay_open && !ui_wants_mouse && mnk_mode && mnk_mouse && mouse_look_active;
}

inline bool MouseMotionIsActivity(float dx, float dy) { return dx != 0.0f || dy != 0.0f; }

// Reports only the start of meaningful controller input, not a held button, stick, or trigger, so mouse
// movement is not overridden by input already down.
class ControllerActivityFilter {
 public:
  bool Update(const rex::input::X_INPUT_GAMEPAD& gamepad);

 private:
  uint16_t buttons_ = 0;
  bool left_trigger_ = false;
  bool right_trigger_ = false;
  bool thumb_lx_ = false;
  bool thumb_ly_ = false;
  bool thumb_rx_ = false;
  bool thumb_ry_ = false;
};

inline bool ControllerActivityFilter::Update(const rex::input::X_INPUT_GAMEPAD& gamepad) {
  const uint16_t buttons = static_cast<uint16_t>(gamepad.buttons);
  bool started = (buttons & ~buttons_) != 0;
  buttons_ = buttons;

  const auto trigger_started = [&started](uint8_t value, bool& was_active) {
    const bool active = value >= rex::input::kTriggerThreshold;
    started = started || (active && !was_active);
    was_active = active;
  };
  const auto thumb_started = [&started](rex::be<int16_t> value, bool& was_active) {
    const bool active = std::abs(static_cast<int>(value.get())) >= rex::input::kThumbDeadzone;
    started = started || (active && !was_active);
    was_active = active;
  };

  trigger_started(gamepad.left_trigger, left_trigger_);
  trigger_started(gamepad.right_trigger, right_trigger_);
  thumb_started(gamepad.thumb_lx, thumb_lx_);
  thumb_started(gamepad.thumb_ly, thumb_ly_);
  thumb_started(gamepad.thumb_rx, thumb_rx_);
  thumb_started(gamepad.thumb_ry, thumb_ry_);
  return started;
}

}  // namespace crackdown
