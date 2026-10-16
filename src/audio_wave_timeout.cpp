#include "audio_wave_timeout.h"

#include <rex/hook.h>
#include <rex/ppc.h>

#include "frame_pacing.h"

REX_EXTERN(__imp__sub_8225AB30);

namespace {

crackdown::WaveTimeoutClock g_clock;

}  // namespace

// Wave instance setup: r3 is the pooled instance.
REX_HOOK_RAW(sub_8225AB30) {
  g_clock.Reset(ctx.r3.u32);
  __imp__sub_8225AB30(ctx, base);
}

// Codegen hook after the `addi r11,r11,1` at 8225B254 in sub_8225B0A8 (config/overrides.toml): r11 is the instance's
// elapsed count and r30 the instance. The compare against the deadline and the stop stay as shipped.
void crackdown_audio_wave_timeout_tick(PPCRegister& elapsed, PPCRegister& instance) {
  elapsed.u64 = elapsed.u32 - 1u + g_clock.Tick(instance.u32, crackdown::OriginalStepsPerUpdate());
}
