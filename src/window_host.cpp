#include "window_host.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include "cursor_hider.h"
#include "input_activity.h"
#include "mouse_look.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

// Above ReXApp's input listener (z-order 0), so F11 is consumed before the guest sees it.
constexpr size_t kWindowHostZOrder = 1000;

// SDL's repeat flag follows its own key bookkeeping, which a fullscreen change can reset.
bool IsF11PhysicallyDown() {
#if defined(_WIN32)
  return (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
#else
  return false;
#endif
}

bool IsMenuKey(rex::ui::VirtualKey key) {
  switch (key) {
    case rex::ui::VirtualKey::kEscape:
    case rex::ui::VirtualKey::kReturn:
    case rex::ui::VirtualKey::kX:
    case rex::ui::VirtualKey::kZ:
    case rex::ui::VirtualKey::kTab:
      return true;
    default:
      return false;
  }
}

bool IsGameplayKey(const rex::ui::KeyEvent& e) {
  switch (e.virtual_key()) {
    case rex::ui::VirtualKey::kNone:
    case rex::ui::VirtualKey::kPacket:
    case rex::ui::VirtualKey::kProcessKey:
    case rex::ui::VirtualKey::kEscape:
    case rex::ui::VirtualKey::kF2:
    case rex::ui::VirtualKey::kF3:
    case rex::ui::VirtualKey::kF4:
    case rex::ui::VirtualKey::kF7:
    case rex::ui::VirtualKey::kF10:
    case rex::ui::VirtualKey::kF11:
    case rex::ui::VirtualKey::kF12:
    case rex::ui::VirtualKey::kOem3:
      return false;
    default:
      return true;
  }
}

#if defined(_WIN32)
HWND NativeHandle(const rex::ui::Window* window) {
  return static_cast<HWND>(window->GetNativeWindowHandle());
}

// SDL's borderless fullscreen is a popup without a caption; a windowed window always has one.
bool IsDecorated(HWND hwnd) {
  return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
}

// Centers a window of the given outer size on the work area of the monitor it is on now (supports a monitor at
// negative coordinates, since it only ever adds a non-negative offset to work.left/top).
RECT CenterOnCurrentMonitor(HWND hwnd, int width, int height) {
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
  const RECT& work = monitor.rcWork;
  width = (std::min)(width, int(work.right - work.left));
  height = (std::min)(height, int(work.bottom - work.top));
  const int left = work.left + (work.right - work.left - width) / 2;
  const int top = work.top + (work.bottom - work.top - height) / 2;
  return {left, top, left + width, top + height};
}

// The windowed rectangle to restore on leaving fullscreen: always centered on the current monitor, so toggling
// repeatedly cannot drift off-center. The size is the last captured windowed size, so a resize survives a toggle;
// with no captured size yet, it is the window's desired size converted from client to outer (frame) size.
RECT RestoredWindowedRect(const rex::ui::Window* window, HWND hwnd, const std::optional<RECT>& captured) {
  if (captured) {
    return CenterOnCurrentMonitor(hwnd, int(captured->right - captured->left), int(captured->bottom - captured->top));
  }
  RECT frame{0, 0, int(window->SizeToPhysical(window->GetDesiredLogicalWidth())),
             int(window->SizeToPhysical(window->GetDesiredLogicalHeight()))};
  AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
  return CenterOnCurrentMonitor(hwnd, int(frame.right - frame.left), int(frame.bottom - frame.top));
}
#endif

void OnControllerActivity(rex::input::DeviceId device, bool synthetic,
                          const rex::input::X_INPUT_GAMEPAD& gamepad);

class WindowHost final : public rex::ui::WindowInputListener, public rex::ui::WindowListener {
 public:
  void Attach(rex::ui::Window* window, rex::ui::WindowedAppContext& context,
              std::function<bool()> ui_wants_mouse, std::function<void()> toggle_window_mode) {
    window_ = window;
    context_.store(&context, std::memory_order_relaxed);
    ui_wants_mouse_ = std::move(ui_wants_mouse);
    toggle_window_mode_ = std::move(toggle_window_mode);
    cursor_ = crackdown::CursorHider{};
    cursor_.SetFocused(window->HasFocus());
    {
      std::lock_guard lock(filters_mutex_);
      filters_.clear();
    }
    active_.store(true, std::memory_order_relaxed);
    window_->AddInputListener(this, kWindowHostZOrder);
    window_->AddListener(this);
    rex::input::SetInputActivityCallback(OnControllerActivity);
    UpdateCursor();
  }

  void Detach() {
    if (!window_) {
      return;
    }
    rex::input::SetInputActivityCallback(nullptr);
    active_.store(false, std::memory_order_relaxed);
    RestoreCursor();
    {
      std::lock_guard lock(filters_mutex_);
      filters_.clear();
    }
    window_->RemoveInputListener(this);
    window_->RemoveListener(this);
    context_.store(nullptr, std::memory_order_relaxed);
    window_ = nullptr;
    ui_wants_mouse_ = nullptr;
    toggle_window_mode_ = nullptr;
  }

  void SetOverlayOpen(bool open) {
    if (!active_.load(std::memory_order_relaxed)) {
      return;
    }
    crackdown::mouse_look::SetBlocked(open);
    cursor_.SetOverlayOpen(open);
    UpdateCursor();
  }

  void OnClosing(rex::ui::UIEvent&) override {
    rex::input::SetInputActivityCallback(nullptr);
    active_.store(false, std::memory_order_relaxed);
    RestoreCursor();
  }

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.target() != window_) {
      return;
    }
    if (IsMenuKey(e.virtual_key())) {
      crackdown::mouse_look::ReleaseCapture();
    }
    if (e.virtual_key() == rex::ui::VirtualKey::kF11) {
      e.set_handled(true);
      const bool physical_down = IsF11PhysicallyDown();
      if (e.prev_state() || (latched_ && physical_down)) {
        return;
      }
      latched_ = true;
      ToggleFullscreen();
      return;
    }
    if (!IsGameplayKey(e) || e.prev_state() || cursor_.overlay_open()) {
      return;
    }
    cursor_.KeyboardInput();
    UpdateCursor();
  }

  void OnKeyUp(rex::ui::KeyEvent& e) override {
    if (e.virtual_key() != rex::ui::VirtualKey::kF11 || e.target() != window_) {
      return;
    }
    e.set_handled(true);
    ReleaseLatchIfPhysicallyUp();
  }

  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (crackdown::MouseMotionIsActivity(e.dx(), e.dy())) {
      NoteMouse(e);
    }
  }
  void OnMouseDown(rex::ui::MouseEvent& e) override { NoteMouse(e); }
  void OnMouseWheel(rex::ui::MouseEvent& e) override { NoteMouse(e); }

  void OnResize(rex::ui::UISetupEvent&) override { ApplyPendingWindowedRect(); }
  void OnGotFocus(rex::ui::UISetupEvent&) override {
    ReleaseLatchIfPhysicallyUp();
    cursor_.SetFocused(true);
    UpdateCursor();
  }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    ReleaseLatchIfPhysicallyUp();
    cursor_.SetFocused(false);
    UpdateCursor();
  }

  void HandleControllerActivity(rex::input::DeviceId device, bool synthetic,
                                const rex::input::X_INPUT_GAMEPAD& gamepad) {
    if (!active_.load(std::memory_order_relaxed) || synthetic) {
      return;
    }
    bool started = false;
    {
      std::lock_guard lock(filters_mutex_);
      started = filters_[static_cast<uint64_t>(device)].Update(gamepad);
    }
    if (!started) {
      return;
    }
    crackdown::mouse_look::ReleaseCapture();
    rex::ui::WindowedAppContext* context = context_.load(std::memory_order_relaxed);
    if (!context) {
      return;
    }
    context->CallInUIThread([this] {
      if (!active_.load(std::memory_order_relaxed)) {
        return;
      }
      cursor_.ControllerInput();
      UpdateCursor();
    });
  }

  // Runs on the UI thread.
  void UpdateCursor() {
    if (!window_ || !active_.load(std::memory_order_relaxed)) {
      return;
    }
    const bool ui_wants_mouse = ui_wants_mouse_ && ui_wants_mouse_();
    crackdown::mouse_look::SetBlocked(cursor_.overlay_open() || ui_wants_mouse);
    const bool mouse_look_owns =
        crackdown::MouseLookOwnsCursor(window_->HasFocus(), cursor_.overlay_open(), ui_wants_mouse,
                                       rex::cvar::Query<bool>("mnk_mode"), rex::cvar::Query<bool>("mnk_mouse"),
                                       rex::input::mnk::IsMouseLookActive());
    const bool visible =
        !mouse_look_owns &&
        (cursor_.last_device() == crackdown::CursorInputDevice::kMouse
             ? (cursor_.visible() || ui_wants_mouse)
             : cursor_.visible());
    const auto wanted =
        visible ? rex::ui::Window::CursorVisibility::kVisible : rex::ui::Window::CursorVisibility::kHidden;
    if (window_->GetCursorVisibility() != wanted) {
      window_->SetCursorVisibility(wanted);
    }
  }

 private:
  void NoteMouse(rex::ui::MouseEvent& e) {
    if (e.target() != window_ || !window_->HasFocus()) {
      return;
    }
    cursor_.MouseMoved();
    UpdateCursor();
  }

  void ToggleFullscreen() {
    crackdown::mouse_look::ReleaseCapture();
    const bool fullscreen = !window_->IsFullscreen();
#if defined(_WIN32)
    if (fullscreen) {
      CaptureWindowedRect();
    }
#endif
    pending_windowed_rect_ = !fullscreen;
    toggle_window_mode_();
    ApplyPendingWindowedRect();
    UpdateCursor();
    REXLOG_INFO("Window mode: {}", fullscreen ? "borderless fullscreen" : "windowed");
  }

  // SDL restores its own windowed rectangle when fullscreen ends, which can be the desktop size. The stored
  // rectangle is applied once the native window is decorated again, so the window is never left at that size.
  void ApplyPendingWindowedRect() {
#if defined(_WIN32)
    if (!pending_windowed_rect_ || window_->IsFullscreen()) {
      return;
    }
    HWND hwnd = NativeHandle(window_);
    if (!hwnd || !IsDecorated(hwnd)) {
      return;
    }
    const RECT rect = RestoredWindowedRect(window_, hwnd, windowed_rect_);
    SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    windowed_rect_ = rect;
    pending_windowed_rect_ = false;
#endif
  }

#if defined(_WIN32)
  void CaptureWindowedRect() {
    HWND hwnd = NativeHandle(window_);
    RECT rect{};
    if (!hwnd || !IsDecorated(hwnd) || IsZoomed(hwnd) || IsIconic(hwnd) || !GetWindowRect(hwnd, &rect)) {
      return;
    }
    windowed_rect_ = rect;
  }
#endif

  void ReleaseLatchIfPhysicallyUp() {
    if (latched_ && !IsF11PhysicallyDown()) {
      latched_ = false;
    }
  }

  void RestoreCursor() {
    if (window_) {
      window_->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
    }
  }

  rex::ui::Window* window_ = nullptr;
  std::atomic<rex::ui::WindowedAppContext*> context_{nullptr};
  std::function<bool()> ui_wants_mouse_;
  std::function<void()> toggle_window_mode_;
  bool latched_ = false;
  bool pending_windowed_rect_ = false;
#if defined(_WIN32)
  std::optional<RECT> windowed_rect_;
#endif
  crackdown::CursorHider cursor_;
  std::atomic<bool> active_{false};
  std::mutex filters_mutex_;
  std::unordered_map<uint64_t, crackdown::ControllerActivityFilter> filters_;
};

WindowHost window_host;

void OnControllerActivity(rex::input::DeviceId device, bool synthetic,
                          const rex::input::X_INPUT_GAMEPAD& gamepad) {
  window_host.HandleControllerActivity(device, synthetic, gamepad);
}

class CursorSync final : public rex::ui::ImGuiDialog {
 public:
  explicit CursorSync(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO&) override { window_host.UpdateCursor(); }
};

}  // namespace

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& context,
                      std::function<bool()> ui_wants_mouse, std::function<void()> toggle_window_mode) {
  window_host.Attach(window, context, std::move(ui_wants_mouse), std::move(toggle_window_mode));
}

void DetachWindowHost() { window_host.Detach(); }

void SetCursorOverlayOpen(bool open) { window_host.SetOverlayOpen(open); }

void CreateCursorSync(rex::ui::ImGuiDrawer* drawer) {
  new CursorSync(drawer);  // retained by the drawer for the session
}
