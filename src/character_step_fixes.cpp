#include "character_step_fixes.h"

#include <rex/ppc.h>
#include <rex/system/kernel_state.h>

#include "frame_pacing.h"
#include "guest_memory.h"

namespace {

uint8_t* GuestBase() {
  return rex::system::kernel_state()->memory()->virtual_membase();
}

}  // namespace

// Codegen hook before the `fdivs f0,f0,f20` at 822B5634 in sub_822B4D40 (config/overrides.toml): f0 holds
// clamp(h, 0, 2) * 0.4 and f20 the step's dt; the negated quotient is the snap-down velocity.
void crackdown_ledge_snap_down(PPCRegister& height_pull) {
  height_pull.f64 =
      double(crackdown::ScaleLedgeSnapDown(float(height_pull.f64), crackdown::OriginalStepsPerUpdate()));
}

// Codegen hooks in sub_822A2C90: before the `fmuls f0,f0,f13` at 822A2DB8 f13 is the blend's speed decay, after the
// `fmuls f13,f13,f0` at 822A2E28 f13 is the fraction of the gap closed this step.
void crackdown_ground_snap_decay(PPCRegister& decay) {
  decay.f64 = double(crackdown::ScaleGroundSnapDecay(float(decay.f64), crackdown::OriginalStepsPerUpdate()));
}

void crackdown_ground_snap_lerp(PPCRegister& fraction) {
  fraction.f64 = double(crackdown::ScaleGroundSnapLerp(float(fraction.f64), crackdown::OriginalStepsPerUpdate()));
}

// Codegen hooks after `addi r11,r11,10` at 822E191C (sub_822E1908, hit-reaction merge window) and `addi r10,r11,15` at
// 822BA48C (sub_822BA470, ragdoll threshold): the register holds frame counter + ticks.
void crackdown_hit_merge_window(PPCRegister& deadline) {
  constexpr uint32_t kTicks = 10;
  const uint32_t ticks = crackdown::ScaleTickWindow(kTicks, crackdown::OriginalStepsPerUpdate());
  deadline.u64 = uint32_t(deadline.u32 - kTicks + ticks);
}

void crackdown_ragdoll_tick_window(PPCRegister& deadline) {
  constexpr uint32_t kTicks = 15;
  const uint32_t ticks = crackdown::ScaleTickWindow(kTicks, crackdown::OriginalStepsPerUpdate());
  deadline.u64 = uint32_t(deadline.u32 - kTicks + ticks);
}

// Codegen hook after `fmuls f0,f0,f23` at 822AA8F4 in sub_822A9E78: f0 = gravity * dt, f13 = the vertical speed
// accumulator (+532), 0 after a grounded step.
void crackdown_first_gravity_step(PPCRegister& gravity_step, PPCRegister& accumulated) {
  gravity_step.f64 = double(crackdown::ScaleFirstGravityStep(float(gravity_step.f64), float(accumulated.f64),
                                                             crackdown::OriginalStepsPerUpdate()));
}

// Codegen hook before the `bl 0x822CC000` at 822BA2E8 in sub_822BA0F8 (start ragdoll): f1 is the step's dt, r29 the
// character, whose byte +4220 is set while it is ragdolled.
void crackdown_ragdoll_pose_dt(PPCRegister& dt, PPCRegister& character) {
  constexpr uint32_t kRagdolledOffset = 4220;
  const bool ragdolled = crackdown::guest::Load8(GuestBase(), character.u32 + kRagdolledOffset) != 0;
  dt.f64 = double(float(crackdown::ScaleRagdollPoseDt(dt.f64, ragdolled, crackdown::OriginalStepsPerUpdate())));
}

// Codegen hook before `addi r9,r1,476` at 823672B0 in sub_82366DC8 (projectile update): the float at r1+476, stored
// at 82366DE4 and read only by the impact ray below, is the step's dt.
void crackdown_projectile_probe_dt(PPCRegister& stack) {
  constexpr uint32_t kProbeDtOffset = 476;
  uint8_t* base = GuestBase();
  const uint32_t slot = stack.u32 + kProbeDtOffset;
  crackdown::guest::StoreFloat(base, slot,
                               crackdown::ScaleProjectileProbeDt(crackdown::guest::LoadFloat(base, slot),
                                                                 crackdown::OriginalStepsPerUpdate()));
}

// Codegen hook before the `fdivs f0,f0,f13` at 8228FDDC in sub_8228F990: f13 is the camera forward axis's y.
void crackdown_camera_lift_divisor(PPCRegister& forward_y) {
  forward_y.f64 = double(crackdown::GuardCameraLiftDivisor(float(forward_y.f64)));
}
