#pragma once

#include <cmath>

namespace crackdown {

// steps is the update's length in the title's original 1/30 s steps; the ledge pull must stay per that step.
constexpr float ScaleLedgeSnapDown(float height_pull, double steps) {
  return steps > 0.0 && steps != 1.0 ? height_pull * float(steps) : height_pull;
}

// A ragdoll restarted on an already-ragdolled character differences a stale pose pair; use the original 30 FPS step.
constexpr double ScaleRagdollPoseDt(double dt, bool already_ragdolled, double steps) {
  return already_ragdolled && steps > 0.0 && steps < 1.0 ? dt / steps : dt;
}

// sub_8228F990 lifts the camera by depth / forward.y; a level camera makes that divisor zero.
inline constexpr float kMinCameraLift = 1e-3f;
inline float GuardCameraLiftDivisor(float forward_y) {
  if (std::fabs(forward_y) >= kMinCameraLift) {
    return forward_y;
  }
  return std::signbit(forward_y) ? -kMinCameraLift : kMinCameraLift;
}

}  // namespace crackdown
