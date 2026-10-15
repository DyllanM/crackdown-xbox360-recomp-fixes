// Compatibility guards for title audio: a null-handle bypass for one audio teardown caller, and
// retirement of replaced music instances so each one is released once.

#include "runtime_health.h"

#include <atomic>
#include <bit>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>

REX_EXTERN(__imp__sub_82A49908);
REX_EXTERN(__imp__sub_82265418);

namespace {

constexpr uint32_t kNullAudioCallerSite = 0x8225D5F0;
constexpr uint32_t kMusicInstanceCount = 0x82DE2538;
constexpr uint32_t kMusicFixLineLimit = 8;

std::atomic<uint32_t> g_music_fix_lines{0};

// The slot teardown at 8225D5F0 selects its no-release path through the out byte, so setting it
// skips the release of the null handle; the slot state word is cleared to leave the slot idle.
void SkipNullAudioObject(uint8_t* base, uint32_t out_flag, uint32_t slot) {
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) {
    REXLOG_INFO("[DEBUG_SAULO][Compat] skipped_null_audio_object caller=8225D5F0 target=82A49908");
  }
  *rex::memory::GuestPtr<uint8_t*>(base, out_flag) = 1;
  std::memset(rex::memory::GuestPtr<uint8_t*>(base, slot + 12), 0, sizeof(uint32_t));
}

uint32_t LoadWord(uint8_t* base, uint32_t addr) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(value));
  return std::byteswap(value);
}

void StoreWord(uint8_t* base, uint32_t addr, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &value, sizeof(value));
}

}  // namespace

// sub_82265418 releases every listed music instance before it starts the new track, and the
// list is never otherwise cleared, so the released entries are reset to keep them from being
// released again on the next track change.
REX_HOOK_RAW(sub_82265418) {
  const uint32_t retired = LoadWord(base, kMusicInstanceCount);
  __imp__sub_82265418(ctx, base);
  StoreWord(base, kMusicInstanceCount, 0);
  if (g_music_fix_lines.fetch_add(1) < kMusicFixLineLimit) {
    REXLOG_INFO("[DEBUG_SAULO][MusicFix] retired_instances={} list_reset=1", retired);
  }
}

// The 031 caller-specific bypass. Other callers of sub_82A49908 keep the original call.
REX_HOOK_RAW(sub_82A49908) {
  if (!ctx.r3.u32) {
    if (uint32_t(ctx.lr) == kNullAudioCallerSite) {
      SkipNullAudioObject(base, ctx.r4.u32, ctx.r31.u32);
      ctx.r3.u64 = 0;
      return;
    }
    REXLOG_ERROR("[DEBUG_SAULO][Fault] function=82A49908 caller={:08X} r1={:08X} "
                 "r3=0 r4={:08X} r29={:08X} r30={:08X} r31={:08X}",
                 uint32_t(ctx.lr), ctx.r1.u32, ctx.r4.u32, ctx.r29.u32, ctx.r30.u32, ctx.r31.u32);
    rex::ObserveRuntime("FAULT", "guest_null_argument", 0x82A49908, ctx.lr, ctx.r1.u32);
  }
  __imp__sub_82A49908(ctx, base);
}
