#include "character_step_fixes.h"

#include <rex/ppc.h>
#include <rex/system/kernel_state.h>

#include "frame_pacing.h"

// Codegen hook before the `fdivs f0,f0,f20` at 822B5634 in sub_822B4D40 (config/overrides.toml): f0 holds
// clamp(h, 0, 2) * 0.4 and f20 the step's dt; the negated quotient is the snap-down velocity.
void crackdown_ledge_snap_down(PPCRegister& height_pull) {
  height_pull.f64 =
      double(crackdown::ScaleLedgeSnapDown(float(height_pull.f64), crackdown::OriginalStepsPerUpdate()));
}

// Codegen hook before the `bl 0x822CC000` at 822BA2E8 in sub_822BA0F8 (start ragdoll): f1 is the step's dt, r29 the
// character, whose byte +4220 is set while it is ragdolled.
void crackdown_ragdoll_pose_dt(PPCRegister& dt, PPCRegister& character) {
  constexpr uint32_t kRagdolledOffset = 4220;
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const bool ragdolled = *rex::memory::GuestPtr<const uint8_t*>(base, character.u32 + kRagdolledOffset) != 0;
  dt.f64 = double(float(crackdown::ScaleRagdollPoseDt(dt.f64, ragdolled, crackdown::OriginalStepsPerUpdate())));
}

// Codegen hook before the `fdivs f0,f0,f13` at 8228FDDC in sub_8228F990: f13 is the camera forward axis's y.
void crackdown_camera_lift_divisor(PPCRegister& forward_y) {
  forward_y.f64 = double(crackdown::GuardCameraLiftDivisor(float(forward_y.f64)));
}
