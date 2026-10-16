// Compatibility guards for title audio: a null-handle bypass for one audio teardown caller, and the escape that
// lets the audio workers leave their rendezvous when the title is terminated.

#include "runtime_health.h"

#include <atomic>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

REX_EXTERN(__imp__sub_82A49908);

namespace {

constexpr uint32_t kNullAudioCallerSite = 0x8225D5F0;

// The slot teardown at 8225D5F0 selects its no-release path through the out byte, which also skips the slot's own
// idle reset (offsets 64, 70-73), so the reset is done here.
void SkipNullAudioObject(uint8_t* base, uint32_t out_flag, uint32_t slot) {
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) {
    REXLOG_INFO("[DEBUG_SAULO][Compat] skipped_null_audio_object caller=8225D5F0 target=82A49908");
  }
  *rex::memory::GuestPtr<uint8_t*>(base, out_flag) = 1;
  std::memset(rex::memory::GuestPtr<uint8_t*>(base, slot + 12), 0, sizeof(uint32_t));
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 64) = 0;
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 70) = 0;
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 71) = 0;
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 72) = 0;
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 73) = 1;
}

}  // namespace

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

// Codegen hook at the spin of sub_82A50000 (config/overrides.toml). The audio workers wait there for each other
// without a kernel wait, so one whose partner already stopped for title termination would run guest code forever.
bool crackdown_audio_barrier_escape() {
  const auto* kernel = rex::system::kernel_state();
  return kernel && kernel->is_terminating_title();
}
