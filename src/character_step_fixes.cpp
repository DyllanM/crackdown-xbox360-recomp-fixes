#include "character_step_fixes.h"

#include <rex/ppc.h>

#include "frame_pacing.h"

// Codegen hook before the `fdivs f0,f0,f20` at 822B5634 in sub_822B4D40 (config/overrides.toml): f0 holds
// clamp(h, 0, 2) * 0.4 and f20 the step's dt; the negated quotient is the snap-down velocity.
void crackdown_ledge_snap_down(PPCRegister& height_pull) {
  height_pull.f64 =
      double(crackdown::ScaleLedgeSnapDown(float(height_pull.f64), crackdown::OriginalStepsPerUpdate()));
}
