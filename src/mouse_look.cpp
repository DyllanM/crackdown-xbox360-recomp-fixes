#include "mouse_look.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

REXCVAR_DECLARE(double, mnk_sensitivity);

namespace crackdown::mouse_look {

namespace {

constexpr uint32_t kCameraTuningPointer = 0x82DE258C;
constexpr uint32_t kPitchMinOffset = 80;
constexpr uint32_t kPitchMaxOffset = 84;
constexpr double kTwoPi = 6.283185307179586;
constexpr auto kStaleAfter = std::chrono::milliseconds(250);

std::atomic<bool> g_mnk_mode{false};
std::atomic<bool> g_mnk_mouse{false};
std::atomic<bool> g_stick_suppressed{true};
std::atomic<bool> g_focused{false};
std::atomic<bool> g_blocked{true};
std::atomic<bool> g_captured{false};
std::atomic<double> g_dpi_scale{1.0};

rex::ui::Window* g_window = nullptr;
std::mutex g_mutex;
Accumulator g_accumulator;
std::chrono::steady_clock::time_point g_last_update{};
bool g_seen_update = false;
bool g_logged = false;

bool DirectEnabled() {
  return g_stick_suppressed.load(std::memory_order_relaxed) &&
         g_mnk_mode.load(std::memory_order_relaxed) &&
         g_mnk_mouse.load(std::memory_order_relaxed);
}

bool Listening() {
  return DirectLookAllowed(DirectEnabled(), g_focused.load(std::memory_order_relaxed),
                           g_blocked.load(std::memory_order_relaxed),
                           g_captured.load(std::memory_order_relaxed));
}

bool Active() {
  return DirectLookAllowed(DirectEnabled(), g_focused.load(std::memory_order_relaxed),
                           g_blocked.load(std::memory_order_relaxed),
                           g_captured.load(std::memory_order_relaxed));
}

void ClearAccumulator() {
  std::lock_guard lock(g_mutex);
  g_accumulator.Clear();
}

void ResetTiming() {
  std::lock_guard lock(g_mutex);
  g_last_update = {};
  g_seen_update = false;
}

bool MarkUpdate(std::chrono::steady_clock::time_point now) {
  std::lock_guard lock(g_mutex);
  const bool stale = g_seen_update && now - g_last_update > kStaleAfter;
  g_last_update = now;
  g_seen_update = true;
  return stale;
}

Delta TakeAccumulator() {
  std::lock_guard lock(g_mutex);
  return g_accumulator.Take();
}

double DpiScale() {
  const double scale = g_dpi_scale.load(std::memory_order_relaxed);
  return scale > 0.25 && std::isfinite(scale) ? scale : 1.0;
}

class Listener final : public rex::ui::WindowInputListener,
                       public rex::ui::WindowListener {
 public:
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (e.target() != g_window || !Listening()) {
      return;
    }
    if (e.dx() == 0.0f && e.dy() == 0.0f) {
      return;
    }
    std::lock_guard lock(g_mutex);
    g_accumulator.Add(e.dx(), e.dy());
  }

  void OnMouseDown(rex::ui::MouseEvent& e) override {
    if (e.target() != g_window || e.button() != rex::ui::MouseEvent::Button::kLeft) {
      return;
    }
    SetCaptured(true);
  }

  void OnGotFocus(rex::ui::UISetupEvent&) override {
    g_focused.store(true, std::memory_order_relaxed);
    ClearAccumulator();
    ResetTiming();
  }

  void OnLostFocus(rex::ui::UISetupEvent&) override {
    g_focused.store(false, std::memory_order_relaxed);
    SetCaptured(false);
    ClearAccumulator();
    ResetTiming();
  }

  void OnDpiChanged(rex::ui::UISetupEvent&) override {
    if (!g_window) {
      return;
    }
    const uint32_t medium = g_window->GetMediumDpi();
    const double scale = medium ? double(g_window->GetDpi()) / double(medium) : 1.0;
    g_dpi_scale.store(scale > 0.25 ? scale : 1.0, std::memory_order_relaxed);
  }
};

Listener g_listener;

void ClampPitchToCameraLimits(float& pitch) {
  auto* kernel = rex::system::kernel_state();
  if (!kernel || !kernel->memory()) {
    return;
  }
  uint8_t* base = kernel->memory()->virtual_membase();
  const uint32_t block = rex::memory::load_and_swap<uint32_t>(base + kCameraTuningPointer);
  if (!block) {
    return;
  }
  const float a = rex::memory::load_and_swap<float>(base + block + kPitchMinOffset);
  const float b = rex::memory::load_and_swap<float>(base + block + kPitchMaxOffset);
  if (!std::isfinite(a) || !std::isfinite(b)) {
    return;
  }
  pitch = std::clamp(pitch, std::min(a, b), std::max(a, b));
}

}  // namespace

void Install(rex::ui::Window* window) {
  if (!window || g_window) {
    return;
  }
  g_window = window;
  g_focused.store(window->HasFocus(), std::memory_order_relaxed);
  const uint32_t medium = window->GetMediumDpi();
  const double scale = medium ? double(window->GetDpi()) / double(medium) : 1.0;
  g_dpi_scale.store(scale > 0.25 ? scale : 1.0, std::memory_order_relaxed);
  window->AddInputListener(&g_listener, 500);
  window->AddListener(&g_listener);
  ResetTiming();
}

void Uninstall() {
  if (!g_window) {
    return;
  }
  SetCaptured(false);
  g_window->RemoveInputListener(&g_listener);
  g_window->RemoveListener(&g_listener);
  g_window = nullptr;
  g_mnk_mode.store(false, std::memory_order_relaxed);
  g_mnk_mouse.store(false, std::memory_order_relaxed);
  g_stick_suppressed.store(true, std::memory_order_relaxed);
  g_focused.store(false, std::memory_order_relaxed);
  g_blocked.store(true, std::memory_order_relaxed);
  ClearAccumulator();
  ResetTiming();
}

void Configure() {
  const bool mnk_mode = rex::cvar::Query<bool>("mnk_mode");
  const bool mnk_mouse = rex::cvar::Query<bool>("mnk_mouse");
  const bool direct = mnk_mode && mnk_mouse;
  const bool suppressed = rex::cvar::SetFlagByName("mnk_mouse_stick", direct ? "false" : "true");
  g_mnk_mode.store(mnk_mode, std::memory_order_relaxed);
  g_mnk_mouse.store(mnk_mouse, std::memory_order_relaxed);
  g_stick_suppressed.store(suppressed, std::memory_order_relaxed);
  g_captured.store(false, std::memory_order_relaxed);
  ClearAccumulator();
  ResetTiming();
  if (!direct) {
    rex::input::mnk::SetMouseLookActive(true);
    return;
  }
  if (!suppressed) {
    rex::input::mnk::SetMouseLookActive(true);
    REXLOG_WARN("Direct mouse look is unavailable: mnk_mouse_stick is not registered");
    return;
  }
  rex::input::mnk::SetMouseLookActive(false);
  if (!g_logged) {
    g_logged = true;
    REXLOG_INFO("Direct mouse look: {:.2f}x, {:.5f} rad per count",
                ClampSensitivity(REXCVAR_GET(mnk_sensitivity)), kRadiansPerCount);
  }
}

std::mutex g_capture_mutex;

void SetCaptured(bool captured) {
  std::lock_guard capture_lock(g_capture_mutex);
  if (captured) {
    if (!CanAcquireCapture(DirectEnabled(), g_focused.load(std::memory_order_relaxed),
                           g_blocked.load(std::memory_order_relaxed))) {
      return;
    }
    if (g_captured.exchange(true, std::memory_order_relaxed)) {
      return;
    }
    ClearAccumulator();
    ResetTiming();
    rex::input::mnk::SetMouseLookActive(true);
    return;
  }

  const bool was_captured = g_captured.exchange(false, std::memory_order_relaxed);
  if (was_captured) {
    ClearAccumulator();
    ResetTiming();
  }
  if (DirectEnabled()) {
    rex::input::mnk::SetMouseLookActive(false);
  }
}

void ReleaseCapture() { SetCaptured(false); }

void SetBlocked(bool blocked) {
  if (g_blocked.exchange(blocked, std::memory_order_relaxed) != blocked) {
    if (blocked) {
      SetCaptured(false);
    }
    ClearAccumulator();
    ResetTiming();
  }
}

bool ApplyTurn(float& yaw, float& pitch) {
  const bool stale = MarkUpdate(std::chrono::steady_clock::now());
  if (!Active() || stale) {
    ClearAccumulator();
    return false;
  }

  const Delta delta = TakeAccumulator();
  const Turn turn = TurnFor(delta, REXCVAR_GET(mnk_sensitivity), DpiScale(), false);
  if (!turn.any()) {
    return false;
  }

  const double wrapped = std::remainder(double(yaw) + turn.yaw, kTwoPi);
  if (std::isfinite(wrapped)) {
    yaw = float(wrapped);
  }
  pitch = float(double(pitch) + turn.pitch);
  ClampPitchToCameraLimits(pitch);
  return true;
}

}  // namespace crackdown::mouse_look

void crackdown_mouse_look_camera_a(PPCRegister& yaw, PPCRegister& pitch) {
  float y = float(yaw.f64);
  float p = float(pitch.f64);
  if (!crackdown::mouse_look::ApplyTurn(y, p)) {
    return;
  }
  yaw.f64 = double(y);
  pitch.f64 = double(p);
}

void crackdown_mouse_look_camera_b(PPCRegister& yaw, PPCRegister& pitch) {
  crackdown_mouse_look_camera_a(yaw, pitch);
}
