#include "frame_rate.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include <fmt/format.h>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "platform.h"
#include "settings.h"

// The SDK's VdSwap body, exported by rexruntime.dll. The SDK has no header for it.
namespace rex::kernel::xboxkrnl {

void VdSwap_entry(mapped_void buffer_ptr, mapped_void fetch_ptr, mapped_void unk2, mapped_void unk3,
                  mapped_void unk4, mapped_u32 frontbuffer_ptr, mapped_u32 texture_format_ptr,
                  mapped_u32 color_space_ptr, mapped_u32 width, mapped_u32 height);

}  // namespace rex::kernel::xboxkrnl

namespace crackdown {
namespace {

constexpr double kSampleSeconds = 1.0;

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_active{false};
rex::ui::Window* g_window = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;

std::mutex g_sample_mutex;
std::optional<Clock::time_point> g_sample_start;
uint64_t g_sample_frames = 0;
uint64_t g_total_frames = 0;
GuestFrameStats g_stats;

void ShowFps(double fps) {
  if (g_active.load(std::memory_order_relaxed)) {
    g_window->SetTitle(REXCVAR_GET(crackdown_show_fps) ? fmt::format("{} | FPS: {:.2f}", kWindowTitle, fps)
                                                        : std::string(kWindowTitle));
  }
}

void CountGuestFrame() {
  if (!g_active.load(std::memory_order_relaxed)) {
    return;
  }
  std::lock_guard lock(g_sample_mutex);
  const auto now = Clock::now();
  if (!g_sample_start) {
    g_sample_start = now;
    return;
  }
  ++g_sample_frames;
  ++g_total_frames;
  const double elapsed = std::chrono::duration<double>(now - *g_sample_start).count();
  if (elapsed < kSampleSeconds) {
    return;
  }
  const double fps = g_sample_frames / elapsed;
  g_stats = {fps, elapsed * 1000.0 / g_sample_frames, g_total_frames};
  g_sample_start = now;
  g_sample_frames = 0;
  g_app_context->CallInUIThread([fps] { ShowFps(fps); });
}

}  // namespace

void AttachFrameRate(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context) {
  g_window = window;
  g_app_context = &app_context;
  g_active.store(true, std::memory_order_relaxed);
}

void DetachFrameRate() {
  g_active.store(false, std::memory_order_relaxed);
}

GuestFrameStats GetGuestFrameStats() {
  std::lock_guard lock(g_sample_mutex);
  return g_stats;
}

}  // namespace crackdown

// Named like the SDK's export, so REX_EXTERN would call this hook again: the original is called by its entry point.
REX_HOOK_RAW(__imp__VdSwap) {
  rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);
  crackdown::CountGuestFrame();
}
