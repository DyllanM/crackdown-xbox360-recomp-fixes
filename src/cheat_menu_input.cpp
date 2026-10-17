#include "cheat_menu_input.h"

#include <bit>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>

#include "cheat_menu_rules.h"

namespace crackdown::cheats {
namespace {

constexpr uint32_t kInputStateGamepadOffset = 4;
constexpr uint32_t kInputStateGamepadSize = 12;
constexpr uint32_t kThumbLeftXOffset = 4;
constexpr uint32_t kThumbLeftYOffset = 6;
constexpr size_t kMaxQueuedEvents = 16;

std::mutex g_mutex;
CheatPadGate g_gate;              // guarded by g_mutex
std::deque<uint8_t> g_events;     // guarded by g_mutex

double Now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint16_t LoadBE16(const uint8_t* base, uint32_t addr) {
  uint16_t value;
  std::memcpy(&value, base + addr, sizeof(value));
  return std::byteswap(value);
}

}  // namespace

void SetMenuInputOpen(bool open) {
  std::lock_guard lock(g_mutex);
  g_gate.SetOpen(open);
  g_events.clear();
}

uint8_t TakeMenuNavEvent() {
  std::lock_guard lock(g_mutex);
  if (g_events.empty()) {
    return 0;
  }
  const uint8_t event = g_events.front();
  g_events.pop_front();
  return event;
}

bool MenuInputHeld() {
  std::lock_guard lock(g_mutex);
  return g_gate.AnyHeld();
}

void FilterMenuInput(uint8_t* base, uint32_t user, uint32_t state) {
  const uint32_t pad = state + kInputStateGamepadOffset;
  const uint16_t buttons = LoadBE16(base, pad);
  const int thumb_x = static_cast<int16_t>(LoadBE16(base, pad + kThumbLeftXOffset));
  const int thumb_y = static_cast<int16_t>(LoadBE16(base, pad + kThumbLeftYOffset));
  CheatPadGate::Verdict verdict;
  {
    std::lock_guard lock(g_mutex);
    verdict = g_gate.Poll(user, buttons, thumb_x, thumb_y, Now());
    for (uint8_t bit = kNavUp; bit <= kNavClose && g_events.size() < kMaxQueuedEvents; bit = uint8_t(bit << 1)) {
      if (verdict.events & bit) {
        g_events.push_back(bit);
      }
    }
  }
  if (verdict.hide_input) {
    std::memset(base + pad, 0, kInputStateGamepadSize);
  }
}

}  // namespace crackdown::cheats
