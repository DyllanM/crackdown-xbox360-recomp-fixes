#pragma once

#include <cmath>
#include <cstdint>

namespace crackdown {

// steps is the update's length in the title's original 1/30 s steps; every function returns its input at 1.0.

// The ledge pull is divided by dt; a shorter step divides it by the title's 1/30 s instead. A longer, caught-up step
// keeps the title's pull, which would otherwise close more than the whole drop in one step.
constexpr float ScaleLedgeSnapDown(float height_pull, double steps) {
  return steps > 0.0 && steps < 1.0 ? height_pull * float(steps) : height_pull;
}

// sub_822A2C90 decays its height blend by c and closes a fraction a of the gap once per step: c^steps and
// 1 - (1 - a)^steps per update.
inline float ScaleGroundSnapDecay(float decay, double steps) {
  if (!(steps > 0.0) || steps == 1.0 || !(decay > 0.0f)) {
    return decay;
  }
  return std::pow(decay, float(steps));
}

inline float ScaleGroundSnapLerp(float fraction, double steps) {
  if (!(steps > 0.0) || steps == 1.0 || !(fraction > 0.0f) || fraction >= 1.0f) {
    return fraction;
  }
  return 1.0f - std::pow(1.0f - fraction, float(steps));
}

// Windows counted in frame-counter ticks, one per update, keep their length in 1/30 s steps.
inline uint32_t ScaleTickWindow(uint32_t ticks, double steps) {
  if (!(steps > 0.0) || steps == 1.0) {
    return ticks;
  }
  return uint32_t(std::lround(double(ticks) / steps));
}

// A grounded step resets the vertical speed accumulator, so the first gravity increment after it is the 30 FPS one;
// at g * dt the character lingers in the ground contact zone where the ledge follower stops it.
constexpr float ScaleFirstGravityStep(float gravity_step, float accumulated, double steps) {
  return accumulated == 0.0f && steps > 0.0 && steps < 1.0 ? gravity_step / float(steps) : gravity_step;
}

// A ragdoll restarted on an already-ragdolled character differences a stale pose pair; use the original 30 FPS step.
constexpr double ScaleRagdollPoseDt(double dt, bool already_ragdolled, double steps) {
  return already_ragdolled && steps > 0.0 && steps < 1.0 ? dt / steps : dt;
}

// Projectiles detonate only through one ray of launch velocity * dt; a shorter ray lets Havok stop the body first.
constexpr float ScaleProjectileProbeDt(float dt, double steps) {
  return steps > 0.0 && steps < 1.0 ? dt / float(steps) : dt;
}

// sub_8228F990 lifts the camera by depth / forward.y; a level camera makes that divisor zero.
inline constexpr float kMinCameraLift = 1e-3f;
inline float GuardCameraLiftDivisor(float forward_y) {
  if (!std::isfinite(forward_y) || std::fabs(forward_y) >= kMinCameraLift) {
    return forward_y;
  }
  return std::signbit(forward_y) ? -kMinCameraLift : kMinCameraLift;
}

}  // namespace crackdown
