#include "fullscreen_toggle.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>

#include <rex/logging.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

// Above ReXApp's input listener (z-order 0), so F11 is consumed before the guest sees it.
constexpr size_t kFullscreenToggleZOrder = 1000;
constexpr uint32_t kDebugLogLimit = 64;

std::atomic<uint32_t> g_debug_log_count{0};

bool ConsumeDebugBudget() {
  return g_debug_log_count++ < kDebugLogLimit;
}

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

// The default windowed rectangle, used when the window was never windowed: the desired logical size, centered
// on the work area of the monitor the window is on.
RECT DefaultWindowedRect(const rex::ui::Window* window, HWND hwnd) {
  RECT frame{0, 0, int(window->SizeToPhysical(window->GetDesiredLogicalWidth())),
             int(window->SizeToPhysical(window->GetDesiredLogicalHeight()))};
  AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
  const RECT& work = monitor.rcWork;
  const int width = (std::min)(int(frame.right - frame.left), int(work.right - work.left));
  const int height = (std::min)(int(frame.bottom - frame.top), int(work.bottom - work.top));
  const int left = work.left + (work.right - work.left - width) / 2;
  const int top = work.top + (work.bottom - work.top - height) / 2;
  return {left, top, left + width, top + height};
}
#endif

class FullscreenToggle final : public rex::ui::WindowInputListener,
                               public rex::ui::WindowListener {
 public:
  void Attach(rex::ui::Window* window) {
    window_ = window;
    window_->AddInputListener(this, kFullscreenToggleZOrder);
    window_->AddListener(this);
  }

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (!IsToggleKey(e)) {
      return;
    }
    e.set_handled(true);
    const bool physical_down = IsF11PhysicallyDown();
    if (e.prev_state() || (latched_ && physical_down)) {
      if (ConsumeDebugBudget()) {
        REXLOG_INFO("[DEBUG_SAULO][F11] keydown ignored: prev_state={} latched={} physical={}",
                    e.prev_state(), latched_, physical_down);
      }
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

  void OnResize(rex::ui::UISetupEvent&) override { ApplyPendingWindowedRect(); }
  void OnGotFocus(rex::ui::UISetupEvent&) override { ReleaseLatchIfPhysicallyUp(); }
  void OnLostFocus(rex::ui::UISetupEvent&) override { ReleaseLatchIfPhysicallyUp(); }

 private:
  bool IsToggleKey(const rex::ui::KeyEvent& e) const {
    return e.virtual_key() == rex::ui::VirtualKey::kF11 && e.target() == window_;
  }

  void ToggleFullscreen() {
    const bool fullscreen = !window_->IsFullscreen();
    LogState("before");
#if defined(_WIN32)
    if (fullscreen) {
      CaptureWindowedRect();
    }
#endif
    pending_windowed_rect_ = !fullscreen;
    window_->SetFullscreen(fullscreen);
    ApplyPendingWindowedRect();
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
    const RECT rect = windowed_rect_ ? *windowed_rect_ : DefaultWindowedRect(window_, hwnd);
    SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    windowed_rect_ = rect;
    pending_windowed_rect_ = false;
    LogState("restored");
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

  void LogState(const char* label) const {
    if (!ConsumeDebugBudget()) {
      return;
    }
#if defined(_WIN32)
    HWND hwnd = NativeHandle(window_);
    RECT outer{};
    RECT client{};
    if (hwnd) {
      GetWindowRect(hwnd, &outer);
      GetClientRect(hwnd, &client);
    }
    REXLOG_INFO(
        "[DEBUG_SAULO][F11] {}: fullscreen={} decorated={} outer=({},{}) {}x{} client={}x{} drawable={}x{} "
        "focus={}",
        label, window_->IsFullscreen(), hwnd && IsDecorated(hwnd), outer.left, outer.top,
        outer.right - outer.left, outer.bottom - outer.top, client.right - client.left,
        client.bottom - client.top, window_->GetActualPhysicalWidth(), window_->GetActualPhysicalHeight(),
        window_->HasFocus());
#else
    REXLOG_INFO("[DEBUG_SAULO][F11] {}: fullscreen={}", label, window_->IsFullscreen());
#endif
  }

  void ReleaseLatchIfPhysicallyUp() {
    if (latched_ && !IsF11PhysicallyDown()) {
      latched_ = false;
    }
  }

  rex::ui::Window* window_ = nullptr;
  bool latched_ = false;
  bool pending_windowed_rect_ = false;
#if defined(_WIN32)
  std::optional<RECT> windowed_rect_;
#endif
};

FullscreenToggle fullscreen_toggle;

}  // namespace

void AttachFullscreenToggle(rex::ui::Window* window) {
  fullscreen_toggle.Attach(window);
}
