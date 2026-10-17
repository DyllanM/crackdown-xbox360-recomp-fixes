#include "window_host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include "cursor_hider.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

// Above ReXApp's input listener (z-order 0), so F11 is consumed before the guest sees it.
constexpr size_t kWindowHostZOrder = 1000;
constexpr std::chrono::milliseconds kCursorTickInterval{250};

// SDL's repeat flag follows its own key bookkeeping, which a fullscreen change can reset.
bool IsF11PhysicallyDown() {
#if defined(_WIN32)
  return (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
#else
  return false;
#endif
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

class WindowHost final : public rex::ui::WindowInputListener, public rex::ui::WindowListener {
 public:
  void Attach(rex::ui::Window* window, rex::ui::WindowedAppContext& context, std::function<bool()> mouse_free,
              std::function<void()> toggle_window_mode) {
    window_ = window;
    context_ = &context;
    mouse_free_ = std::move(mouse_free);
    toggle_window_mode_ = std::move(toggle_window_mode);
    window_->AddInputListener(this, kWindowHostZOrder);
    window_->AddListener(this);
    cursor_thread_ = std::thread([this] { CursorTickLoop(); });
  }

  void Detach() {
    if (!window_) {
      return;
    }
    {
      std::lock_guard lock(tick_mutex_);
      stopping_ = true;
    }
    tick_wake_.notify_all();
    if (cursor_thread_.joinable()) {
      cursor_thread_.join();
    }
    window_->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
    window_->RemoveInputListener(this);
    window_->RemoveListener(this);
    window_ = nullptr;
  }

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (!IsToggleKey(e)) {
      return;
    }
    e.set_handled(true);
    const bool physical_down = IsF11PhysicallyDown();
    if (e.prev_state() || (latched_ && physical_down)) {
      return;
    }
    latched_ = true;
    ToggleFullscreen();
  }

  void OnKeyUp(rex::ui::KeyEvent& e) override {
    if (!IsToggleKey(e)) {
      return;
    }
    e.set_handled(true);
    ReleaseLatchIfPhysicallyUp();
  }

  void OnMouseMove(rex::ui::MouseEvent&) override {
    std::lock_guard lock(cursor_mutex_);
    cursor_.MouseMoved(crackdown::CursorHider::Clock::now());
    UpdateCursorLocked();
  }

  void OnResize(rex::ui::UISetupEvent&) override { ApplyPendingWindowedRect(); }
  void OnGotFocus(rex::ui::UISetupEvent&) override {
    ReleaseLatchIfPhysicallyUp();
    UpdateCursor();
  }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    ReleaseLatchIfPhysicallyUp();
    UpdateCursor();
  }

 private:
  bool IsToggleKey(const rex::ui::KeyEvent& e) const {
    return e.virtual_key() == rex::ui::VirtualKey::kF11 && e.target() == window_;
  }

  void ToggleFullscreen() {
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

  // Runs on the UI thread.
  void UpdateCursor() {
    std::lock_guard lock(cursor_mutex_);
    UpdateCursorLocked();
  }

  // Mouse look captures and hides the cursor for aiming, so the auto-hide policy leaves it alone.
  bool MouseLookAims(bool gameplay) const {
    return gameplay && window_->HasFocus() && rex::cvar::Query<bool>("mnk_mode") &&
           rex::cvar::Query<bool>("mnk_mouse") && rex::input::mnk::IsMouseLookActive();
  }

  void UpdateCursorLocked() {
    if (!window_) {
      return;
    }
    const bool gameplay = !mouse_free_ || mouse_free_();
    if (MouseLookAims(gameplay)) {
      return;
    }
    const auto visibility = cursor_.Update(gameplay, window_->IsFullscreen(), window_->HasFocus(),
                                           crackdown::CursorHider::Clock::now());
    if (visibility) {
      window_->SetCursorVisibility(*visibility ? rex::ui::Window::CursorVisibility::kVisible
                                               : rex::ui::Window::CursorVisibility::kHidden);
    }
  }

  // SDL gives no timer event, so a host thread asks the UI thread to check the idle time a few times a second. This
  // is not an ImGui dialog: a permanent dialog makes the presenter repaint on every vertical blank.
  void CursorTickLoop() {
    std::unique_lock lock(tick_mutex_);
    while (!tick_wake_.wait_for(lock, kCursorTickInterval, [this] { return stopping_; })) {
      lock.unlock();
      context_->CallInUIThread([this] {
        std::lock_guard guard(tick_mutex_);
        if (!stopping_) {
          UpdateCursor();
        }
      });
      lock.lock();
    }
  }

  rex::ui::Window* window_ = nullptr;
  rex::ui::WindowedAppContext* context_ = nullptr;
  std::function<bool()> mouse_free_;
  std::function<void()> toggle_window_mode_;
  bool latched_ = false;
  bool pending_windowed_rect_ = false;
#if defined(_WIN32)
  std::optional<RECT> windowed_rect_;
#endif
  std::mutex cursor_mutex_;
  crackdown::CursorHider cursor_;
  std::thread cursor_thread_;
  std::mutex tick_mutex_;
  std::condition_variable tick_wake_;
  bool stopping_ = false;
};

WindowHost window_host;

}  // namespace

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& context, std::function<bool()> mouse_free,
                      std::function<void()> toggle_window_mode) {
  window_host.Attach(window, context, std::move(mouse_free), std::move(toggle_window_mode));
}

void DetachWindowHost() {
  window_host.Detach();
}
