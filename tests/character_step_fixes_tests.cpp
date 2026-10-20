#include <cmath>

#include "character_step_fixes.h"
#include "check.h"

using namespace crackdown;

int main() {
  for (const float pull : {0.0f, 0.1f, 0.4f, 0.8f, 0.123456f}) {
    CHECK(ScaleLedgeSnapDown(pull, 1.0) == pull);
  }

  const float pull = 0.8f;
  CHECK(std::fabs(ScaleLedgeSnapDown(pull, 0.25) / (1.0 / 120.0) - pull * 30.0) < 1e-4);
  CHECK(std::fabs(ScaleLedgeSnapDown(pull, 0.5) / (1.0 / 60.0) - pull * 30.0) < 1e-4);
  CHECK(std::fabs(ScaleLedgeSnapDown(pull, 2.0) / (2.0 / 30.0) - pull * 30.0) < 1e-4);

  CHECK(ScaleLedgeSnapDown(pull, 0.0) == pull);
  CHECK(ScaleLedgeSnapDown(pull, -1.0) == pull);

  // A first ragdoll start keeps the title's dt at every rate.
  CHECK(ScaleRagdollPoseDt(1.0 / 30.0, false, 1.0) == 1.0 / 30.0);
  CHECK(ScaleRagdollPoseDt(1.0 / 60.0, false, 0.5) == 1.0 / 60.0);
  CHECK(ScaleRagdollPoseDt(1.0 / 120.0, false, 0.25) == 1.0 / 120.0);

  // An already-ragdolled character uses the original 30 FPS step.
  CHECK(ScaleRagdollPoseDt(1.0 / 30.0, true, 1.0) == 1.0 / 30.0);
  CHECK(ScaleRagdollPoseDt(1.0 / 60.0, true, 0.5) == (1.0 / 60.0) / 0.5);
  CHECK(ScaleRagdollPoseDt(1.0 / 120.0, true, 0.25) == (1.0 / 120.0) / 0.25);

  // Caught-up and invalid steps keep the title's dt.
  CHECK(ScaleRagdollPoseDt(2.0 / 30.0, true, 2.0) == 2.0 / 30.0);
  CHECK(ScaleRagdollPoseDt(1.0 / 120.0, true, 0.0) == 1.0 / 120.0);
  CHECK(ScaleRagdollPoseDt(1.0 / 120.0, true, -1.0) == 1.0 / 120.0);

  CHECK(GuardCameraLiftDivisor(0.5f) == 0.5f);
  CHECK(GuardCameraLiftDivisor(-0.2f) == -0.2f);
  CHECK(GuardCameraLiftDivisor(0.0f) == kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(-0.0f) == -kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(1e-6f) == kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(-1e-6f) == -kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(kMinCameraLift) == kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(-kMinCameraLift) == -kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(2.5f) == 2.5f);
  CHECK(GuardCameraLiftDivisor(-2.5f) == -2.5f);

  return check::Finish("character_step_fixes_tests");
}
