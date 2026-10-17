#include "achievements_input.h"

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <utility>

#include <rex/hook.h>
#include <rex/ppc.h>

#include "achievements_overlay_rules.h"

namespace crackdown {
namespace {

constexpr uint32_t kInputStateGamepadOffset = 4;
constexpr uint32_t kInputStateGamepadSize = 12;
constexpr uint32_t kThumbLeftYOffset = 6;

std::mutex g_gate_mutex;
AchievementsOverlayGate g_gate;  // guarded by g_gate_mutex
std::function<void()> g_close_request;
std::atomic<float> g_scroll_axis{0.0f};

uint16_t LoadBE16(const uint8_t* base, uint32_t addr) {
  uint16_t value;
  std::memcpy(&value, base + addr, sizeof(value));
  return std::byteswap(value);
}

void StoreBE16(uint8_t* base, uint32_t addr, uint16_t value) {
  value = std::byteswap(value);
  std::memcpy(base + addr, &value, sizeof(value));
}

void FilterGuestInput(PPCContext& ctx, uint8_t* base, uint32_t user, uint32_t state) {
  if (!state || ctx.r3.u32 != 0) return;
  const uint16_t buttons = LoadBE16(base, state + kInputStateGamepadOffset);
  const int thumb_y = static_cast<int16_t>(LoadBE16(base, state + kInputStateGamepadOffset + kThumbLeftYOffset));
  AchievementsOverlayGate::Verdict verdict;
  {
    std::lock_guard lock(g_gate_mutex);
    verdict = g_gate.Poll(user, buttons, thumb_y);
  }
  g_scroll_axis.store(verdict.scroll, std::memory_order_relaxed);
  if (verdict.close && g_close_request) g_close_request();
  if (verdict.hide_input) {
    std::memset(base + state + kInputStateGamepadOffset, 0, kInputStateGamepadSize);
  } else if (verdict.clear_b) {
    StoreBE16(base, state + kInputStateGamepadOffset, buttons & ~kXinputButtonB);
  }
}

}  // namespace

void SetAchievementsCloseRequest(std::function<void()> request) { g_close_request = std::move(request); }

void SetAchievementsModal(bool modal) {
  {
    std::lock_guard lock(g_gate_mutex);
    g_gate.SetOpen(modal);
  }
  g_scroll_axis.store(0.0f, std::memory_order_relaxed);
}

float AchievementsScrollAxis() { return g_scroll_axis.load(std::memory_order_relaxed); }

}  // namespace crackdown

// The game's XInputGetState wrapper: r3 is the user, r4 the state, and it passes flags 0 to XamInputGetState.
REX_EXTERN(__imp__sub_82743798);
REX_HOOK_RAW(sub_82743798) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  __imp__sub_82743798(ctx, base);
  crackdown::FilterGuestInput(ctx, base, user, state);
}
