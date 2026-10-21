#include "main_menu_quit.h"

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string_view>
#include <utility>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_memory.h"

REX_EXTERN(__imp__sub_82381F28);
REX_EXTERN(__imp__sub_82658560);
REX_EXTERN(__imp__sub_826586B0);
REX_EXTERN(__imp__sub_826587F8);
REX_EXTERN(__imp__sub_82678768);
REX_EXTERN(__imp__sub_82677CB8);

namespace crackdown::main_menu_quit {

std::atomic<bool> g_native_confirmation{false};

namespace {

using guest::Load32;
using guest::Plausible;

constexpr uint32_t kHelpId = 878;
constexpr uint32_t kSaveAndQuitId = 655;
constexpr uint32_t kQuitId = 656;
constexpr uint32_t kSavedUnits = 64;

struct StringOverride {
  uint32_t id = 0;
  uint32_t address = 0;
  uint32_t length = 0;
  uint16_t original[kSavedUnits]{};
  bool active = false;
};

std::atomic<bool> g_main_menu_active{false};
std::mutex g_handler_mutex;
std::function<void()> g_quit_handler;

StringOverride g_help_override{kHelpId};
StringOverride g_save_and_quit_override{kSaveAndQuitId};
StringOverride g_quit_override{kQuitId};

uint16_t LoadGuest16(uint8_t* base, uint32_t address) {
  return uint16_t(guest::Load8(base, address) << 8 | guest::Load8(base, address + 1));
}

void StoreGuest16(uint8_t* base, uint32_t address, uint16_t value) {
  const uint16_t be = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, address), &be, sizeof(be));
}

void StoreWide(uint8_t* base, uint32_t address, std::u16string_view text) {
  for (size_t i = 0; i < text.size(); ++i) {
    StoreGuest16(base, address + uint32_t(i * 2), uint16_t(text[i]));
  }
  StoreGuest16(base, address + uint32_t(text.size() * 2), 0);
}

void OverrideString(StringOverride& override, uint8_t* base, uint32_t address, std::u16string_view text) {
  if (!Plausible(address)) {
    return;
  }
  if (!override.active) {
    override.address = address;
    override.length = 0;
    while (override.length < kSavedUnits) {
      const uint16_t unit = LoadGuest16(base, address + override.length * 2);
      override.original[override.length++] = unit;
      if (unit == 0) {
        break;
      }
    }
    override.active = true;
  }
  StoreWide(base, address, text);
}

void RestoreString(StringOverride& override, uint8_t* base) {
  if (!override.active) {
    return;
  }
  for (uint32_t i = 0; i < override.length; ++i) {
    StoreGuest16(base, override.address + i * 2, override.original[i]);
  }
  override.active = false;
  override.address = 0;
  override.length = 0;
}

void ApplyStringOverride(uint8_t* base, uint32_t id, uint32_t address) {
  if (id == kHelpId && g_main_menu_active.load(std::memory_order_acquire)) {
    OverrideString(g_help_override, base, address, u"Quit the game");
  } else if (g_native_confirmation.load(std::memory_order_acquire)) {
    if (id == kSaveAndQuitId) {
      OverrideString(g_save_and_quit_override, base, address, u"Quit Game");
    } else if (id == kQuitId) {
      OverrideString(g_quit_override, base, address, u"Quit Game");
    }
  }
}

}  // namespace

void InvokeQuitHandler() {
  std::function<void()> handler;
  {
    std::lock_guard lock(g_handler_mutex);
    handler = g_quit_handler;
  }
  if (handler) {
    handler();
  }
}

bool NativeConfirmationActive() { return g_native_confirmation.load(std::memory_order_acquire); }

void SetQuitHandler(std::function<void()> handler) {
  std::lock_guard lock(g_handler_mutex);
  g_quit_handler = std::move(handler);
}

void MarkMainMenuActive() { g_main_menu_active.store(true, std::memory_order_release); }

void LeaveMainMenu(uint8_t* base) {
  g_main_menu_active.store(false, std::memory_order_release);
  g_native_confirmation.store(false, std::memory_order_release);
  RestoreString(g_help_override, base);
  RestoreString(g_save_and_quit_override, base);
  RestoreString(g_quit_override, base);
}

bool BeginNativeConfirmation() {
  if (g_native_confirmation.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }
  return true;
}

void EndNativeConfirmation(uint8_t* base) {
  g_native_confirmation.store(false, std::memory_order_release);
  RestoreString(g_save_and_quit_override, base);
  RestoreString(g_quit_override, base);
}

void Install() {}

}  // namespace crackdown::main_menu_quit

// The quit block normally disables the control when the frontend session flag is clear. Take its existing
// event-handler branch instead, then continue through the game's own enable call.
void crackdown_main_menu_quit_use_handler() {
  crackdown::main_menu_quit::MarkMainMenuActive();
}

void crackdown_main_menu_quit_handler_attached() {}

void crackdown_main_menu_quit_enable(PPCRegister& enabled, PPCRegister& control, PPCRegister& self) {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t quit = crackdown::guest::Load32(base, self.u32 + 0x10);
  if (crackdown::guest::Plausible(quit)) {
    control.u64 = quit;
  }
  enabled.u64 = 1;
}

void crackdown_main_menu_quit_popup_state(PPCRegister& state) {
  state.u64 = 2;
}

REX_HOOK_RAW(sub_82678768) {
  if (!crackdown::main_menu_quit::BeginNativeConfirmation()) {
    ctx.r3.u64 = 1;
    return;
  }
  __imp__sub_82678768(ctx, base);
}

REX_HOOK_RAW(sub_826587F8) {
  if (!crackdown::main_menu_quit::NativeConfirmationActive()) {
    __imp__sub_826587F8(ctx, base);
    return;
  }
  crackdown::main_menu_quit::EndNativeConfirmation(base);
  __imp__sub_826587F8(ctx, base);
  crackdown::main_menu_quit::InvokeQuitHandler();
}

REX_HOOK_RAW(sub_826586B0) {
  if (!crackdown::main_menu_quit::NativeConfirmationActive()) {
    __imp__sub_826586B0(ctx, base);
    return;
  }
  crackdown::main_menu_quit::EndNativeConfirmation(base);
  __imp__sub_826586B0(ctx, base);
}

REX_HOOK_RAW(sub_82658560) {
  const bool pending = crackdown::main_menu_quit::NativeConfirmationActive();
  __imp__sub_82658560(ctx, base);
  if (pending) {
    crackdown::main_menu_quit::EndNativeConfirmation(base);
  }
}

REX_HOOK_RAW(sub_82381F28) {
  const uint32_t id = ctx.r3.u32;
  __imp__sub_82381F28(ctx, base);
  crackdown::main_menu_quit::ApplyStringOverride(base, id, ctx.r3.u32);
}

REX_HOOK_RAW(sub_82677CB8) {
  crackdown::main_menu_quit::LeaveMainMenu(base);
  __imp__sub_82677CB8(ctx, base);
}
