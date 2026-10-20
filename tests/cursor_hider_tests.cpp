// Cursor last-input behavior: the state machine, per-device controller filter and the window wiring.

#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "check.h"
#include "cursor_hider.h"

using namespace crackdown;

namespace {

const std::optional<bool> kHide = false;
const std::optional<bool> kShow = true;

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(CRACKDOWN_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string ReadPatch(const char* name) {
  std::ifstream in(std::string(CRACKDOWN_PATCH_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

std::string Between(const std::string& source, const char* from, const char* to) {
  const size_t start = source.find(from);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find(to, start + 1);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

rex::input::X_INPUT_GAMEPAD Pad(uint16_t buttons = 0, int16_t thumb_lx = 0, int16_t thumb_ly = 0,
                                int16_t thumb_rx = 0, int16_t thumb_ry = 0, uint8_t left_trigger = 0,
                                uint8_t right_trigger = 0) {
  rex::input::X_INPUT_GAMEPAD pad{};
  pad.buttons.set(buttons);
  pad.thumb_lx.set(thumb_lx);
  pad.thumb_ly.set(thumb_ly);
  pad.thumb_rx.set(thumb_rx);
  pad.thumb_ry.set(thumb_ry);
  pad.left_trigger = left_trigger;
  pad.right_trigger = right_trigger;
  return pad;
}

void TestCursorHider() {
  CursorHider initial;
  CHECK(initial.visible());
  CHECK(initial.last_device() == CursorInputDevice::kMouse);
  CHECK(initial.MouseMoved() == std::nullopt);

  CursorHider last;
  CHECK(last.KeyboardInput() == kHide);
  CHECK(last.last_device() == CursorInputDevice::kKeyboard);
  CHECK(last.ControllerInput() == std::nullopt);
  CHECK(last.last_device() == CursorInputDevice::kController);
  CHECK(last.MouseMoved() == kShow);

  CursorHider button;
  CHECK(button.KeyboardInput() == kHide);
  CHECK(button.MouseButtonPressed() == kShow);
  CursorHider wheel;
  CHECK(wheel.ControllerInput() == kHide);
  CHECK(wheel.MouseWheelMoved() == kShow);
}

void TestOverlay() {
  CursorHider hider;
  CHECK(hider.ControllerInput() == kHide);
  CHECK(hider.SetOverlayOpen(true) == kShow);
  CHECK(hider.overlay_open());
  CHECK(hider.MouseMoved() == std::nullopt);
  CHECK(hider.MouseButtonPressed() == std::nullopt);
  CHECK(hider.MouseWheelMoved() == std::nullopt);
  CHECK(hider.KeyboardInput() == std::nullopt);
  CHECK(hider.ControllerInput() == std::nullopt);
  CHECK(hider.last_device() == CursorInputDevice::kController);
  CHECK(hider.SetOverlayOpen(false) == kHide);

  CHECK(hider.MouseMoved() == kShow);
  CHECK(hider.SetOverlayOpen(true) == std::nullopt);
  CHECK(hider.SetOverlayOpen(false) == std::nullopt);
  CHECK(hider.visible());
}

void TestFocus() {
  CursorHider hider;
  CHECK(hider.ControllerInput() == kHide);
  CHECK(hider.SetFocused(false) == kShow);
  CHECK(hider.MouseMoved() == std::nullopt);
  CHECK(hider.last_device() == CursorInputDevice::kController);
  CHECK(hider.SetFocused(true) == kHide);
  CHECK(hider.MouseMoved() == kShow);
}

void TestControllerButtons() {
  using namespace rex::input;
  ControllerActivityFilter filter;
  CHECK(!filter.Update(Pad()));
  CHECK(filter.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(!filter.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(filter.Update(Pad(X_INPUT_GAMEPAD_A | X_INPUT_GAMEPAD_B)));
  CHECK(!filter.Update(Pad(X_INPUT_GAMEPAD_B)));
  CHECK(filter.Update(Pad(X_INPUT_GAMEPAD_B | X_INPUT_GAMEPAD_DPAD_UP)));
  CHECK(!filter.Update(Pad(X_INPUT_GAMEPAD_B | X_INPUT_GAMEPAD_DPAD_UP)));
  CHECK(filter.Update(Pad(X_INPUT_GAMEPAD_DPAD_LEFT)));
  CHECK(!filter.Update(Pad()));
}

void TestControllerSticks() {
  using namespace rex::input;
  const auto axis_pad = [](int axis, int16_t value) {
    switch (axis) {
      case 0:
        return Pad(0, value, 0, 0, 0);
      case 1:
        return Pad(0, 0, value, 0, 0);
      case 2:
        return Pad(0, 0, 0, value, 0);
      default:
        return Pad(0, 0, 0, 0, value);
    }
  };
  for (int axis = 0; axis < 4; ++axis) {
    ControllerActivityFilter filter;
    CHECK(!filter.Update(axis_pad(axis, 0)));
    CHECK(!filter.Update(axis_pad(axis, kThumbDeadzone - 1)));
    CHECK(filter.Update(axis_pad(axis, kThumbDeadzone)));
    CHECK(!filter.Update(axis_pad(axis, kThumbDeadzone + 2000)));
    CHECK(!filter.Update(axis_pad(axis, 0)));
    CHECK(filter.Update(axis_pad(axis, -kThumbDeadzone)));
    CHECK(!filter.Update(axis_pad(axis, -kThumbDeadzone)));
    CHECK(!filter.Update(axis_pad(axis, 0)));
    CHECK(filter.Update(axis_pad(axis, kThumbDeadzone)));
  }
}

void TestControllerTriggers() {
  using namespace rex::input;
  ControllerActivityFilter filter;
  CHECK(!filter.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold - 1, kTriggerThreshold - 1)));
  CHECK(filter.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold, 0)));
  CHECK(!filter.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold, 0)));
  CHECK(!filter.Update(Pad(0, 0, 0, 0, 0, 0, 0)));
  CHECK(filter.Update(Pad(0, 0, 0, 0, 0, 0, kTriggerThreshold)));
  CHECK(!filter.Update(Pad(0, 0, 0, 0, 0, 0, kTriggerThreshold + 1)));
}

void TestControllerDrift() {
  using namespace rex::input;
  ControllerActivityFilter filter;
  for (int16_t value : {0, 100, -100, 1000, -1000, kThumbDeadzone - 1, -(kThumbDeadzone - 1), 0}) {
    CHECK(!filter.Update(Pad(0, value, 0, 0, 0)));
  }
}

void TestControllerDevicesAreIndependent() {
  using namespace rex::input;
  ControllerActivityFilter first;
  ControllerActivityFilter second;
  CHECK(first.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(!second.Update(Pad()));
  CHECK(!first.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(second.Update(Pad(X_INPUT_GAMEPAD_B)));
  CHECK(!first.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(!second.Update(Pad(X_INPUT_GAMEPAD_B)));
  CHECK(first.Update(Pad(X_INPUT_GAMEPAD_X)));
}

void TestMouseLookPolicy() {
  CHECK(MouseLookOwnsCursor(true, false, false, true, true, true));
  CHECK(!MouseLookOwnsCursor(true, false, false, true, true, false));
  CHECK(!MouseLookOwnsCursor(false, false, false, true, true, true));
  CHECK(!MouseLookOwnsCursor(true, true, false, true, true, true));
  CHECK(!MouseLookOwnsCursor(true, false, true, true, true, true));
  CHECK(!MouseLookOwnsCursor(true, false, false, false, true, true));
  CHECK(!MouseLookOwnsCursor(true, false, false, true, false, true));

  CursorHider hider;
  CHECK(hider.KeyboardInput() == kHide);
  CHECK(MouseLookOwnsCursor(true, false, false, true, true, true));
  CHECK(!hider.visible());
  CHECK(!MouseLookOwnsCursor(true, false, false, true, true, false));
  CHECK(!hider.visible());
  CHECK(hider.MouseMoved() == kShow);
}

void TestWindowModes() {
  CursorHider windowed;
  CursorHider fullscreen;
  CHECK(windowed.ControllerInput() == kHide);
  CHECK(fullscreen.ControllerInput() == kHide);
  CHECK(windowed.MouseMoved() == kShow);
  CHECK(fullscreen.MouseMoved() == kShow);

  const std::string host = ReadSource("window_host.cpp");
  CHECK(!Has(Between(host, "void UpdateCursor()", "void NoteMouse"), "IsFullscreen"));
}

void TestCursorWiring() {
  const std::string host = ReadSource("window_host.cpp");
  CHECK(Has(host, "void OnMouseMove(rex::ui::MouseEvent& e) override"));
  CHECK(Has(host, "void OnMouseDown(rex::ui::MouseEvent& e) override"));
  CHECK(Has(host, "void OnMouseWheel(rex::ui::MouseEvent& e) override"));
  CHECK(Has(host, "void OnKeyDown(rex::ui::KeyEvent& e) override"));
  CHECK(Has(host, "void OnLostFocus(rex::ui::UISetupEvent&) override"));
  CHECK(Has(host, "void OnGotFocus(rex::ui::UISetupEvent&) override"));
  CHECK(Has(host, "SetInputActivityCallback(OnControllerActivity)"));
  CHECK(Has(host, "MouseLookOwnsCursor"));
  CHECK(Has(host, "mnk_mode"));
  CHECK(Has(host, "mnk_mouse"));
  CHECK(Has(host, "IsMouseLookActive"));
  CHECK(Has(host, "CreateCursorSync"));

  const std::string update = Between(host, "void UpdateCursor()", "void NoteMouse");
  const size_t look = update.find("MouseLookOwnsCursor");
  const size_t set = update.find("SetCursorVisibility");
  CHECK(look != std::string::npos && set != std::string::npos && look < set);

  const std::string app = ReadSource("crackdown_app.h");
  CHECK(Has(app, "AttachWindowHost("));
  CHECK(Has(app, "WantCaptureMouse"));
  CHECK(Has(app, "CreateCursorSync(imgui_drawer())"));
  CHECK(Has(app, "SetCursorOverlayOpen(launcher_open_)"));
  CHECK(Has(app, "SetCursorOverlayOpen(true)"));
  CHECK(Has(app, "SetCursorOverlayOpen(false)"));
  CHECK(Has(app, "DetachWindowHost();"));
}

void TestNoTimeoutRemains() {
  const std::string host = ReadSource("window_host.cpp");
  const std::string header = ReadSource("cursor_hider.h");
  CHECK(!Has(host, "kCursorHideDelay"));
  CHECK(!Has(host, "CursorHider::Clock"));
  CHECK(!Has(host, "last_motion_"));
  CHECK(!Has(host, "kCursorTickInterval"));
  CHECK(!Has(host, "cursor_thread_"));
  CHECK(!Has(host, "tick_mutex_"));
  CHECK(!Has(host, "tick_wake_"));
  CHECK(!Has(host, "CursorTickLoop"));
  CHECK(!Has(host, "steady_clock"));
  CHECK(!Has(header, "kCursorHideDelay"));
  CHECK(!Has(header, "Clock"));
  CHECK(!Has(header, "last_motion_"));
  CHECK(!Has(header, "chrono"));
}

void TestInputActivityPatch() {
  const std::string patch = ReadPatch("0094-input-activity-callback.patch");
  CHECK(!patch.empty());
  CHECK(Has(patch, "SetInputActivityCallback"));
  CHECK(Has(patch, "NotifyInputActivity"));
  CHECK(Has(patch, "std::memcmp"));
  CHECK(Has(patch, "g_input_activity_callback(id, gamepad)"));
  CHECK(Has(patch, "NotifyInputActivity(id, state.gamepad)"));
}

}  // namespace

int main() {
  TestCursorHider();
  TestOverlay();
  TestFocus();
  TestControllerButtons();
  TestControllerSticks();
  TestControllerTriggers();
  TestControllerDrift();
  TestControllerDevicesAreIndependent();
  TestMouseLookPolicy();
  TestWindowModes();
  TestCursorWiring();
  TestNoTimeoutRemains();
  TestInputActivityPatch();
  return check::Finish("cursor_hider_tests");
}
