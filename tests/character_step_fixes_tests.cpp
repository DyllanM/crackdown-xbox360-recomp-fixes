#include <cmath>

#include "character_step_fixes.h"
#include "check.h"

using namespace crackdown;

namespace {

void TestLedgeSnapDown() {
  for (const float pull : {0.0f, 0.1f, 0.4f, 0.8f, 0.123456f}) {
    CHECK(ScaleLedgeSnapDown(pull, 1.0) == pull);
  }

  const float pull = 0.8f;
  CHECK(std::fabs(ScaleLedgeSnapDown(pull, 0.25) / (1.0 / 120.0) - pull * 30.0) < 1e-4);
  CHECK(std::fabs(ScaleLedgeSnapDown(pull, 0.5) / (1.0 / 60.0) - pull * 30.0) < 1e-4);

  // A caught-up step keeps the title's per-step pull instead of closing more than the drop.
  CHECK(ScaleLedgeSnapDown(pull, 2.0) == pull);
  CHECK(ScaleLedgeSnapDown(pull, 3.0) == pull);

  CHECK(ScaleLedgeSnapDown(pull, 0.0) == pull);
  CHECK(ScaleLedgeSnapDown(pull, -1.0) == pull);
}

void TestGroundSnapDecay() {
  for (const float decay : {0.01f, 0.5f, 0.9f, 1.0f}) {
    CHECK(ScaleGroundSnapDecay(decay, 1.0) == decay);
  }
  // Four 120 FPS steps decay as much as one 30 FPS step.
  float blend = 1.0f;
  for (int i = 0; i < 4; ++i) {
    blend *= ScaleGroundSnapDecay(0.6f, 0.25);
  }
  CHECK(std::fabs(blend - 0.6f) < 1e-5);
  CHECK(ScaleGroundSnapDecay(1.0f, 0.25) == 1.0f);
  CHECK(ScaleGroundSnapDecay(0.0f, 0.25) == 0.0f);
  CHECK(std::isnan(ScaleGroundSnapDecay(NAN, 0.25)));
}

void TestGroundSnapLerp() {
  for (const float fraction : {0.0f, 0.2f, 0.5f, 1.0f}) {
    CHECK(ScaleGroundSnapLerp(fraction, 1.0) == fraction);
  }
  // Four 120 FPS steps close the same share of the gap as one 30 FPS step.
  float gap = 1.0f;
  for (int i = 0; i < 4; ++i) {
    gap *= 1.0f - ScaleGroundSnapLerp(0.4f, 0.25);
  }
  CHECK(std::fabs(gap - 0.6f) < 1e-5);
  CHECK(ScaleGroundSnapLerp(1.0f, 0.25) == 1.0f);
  CHECK(ScaleGroundSnapLerp(0.0f, 0.25) == 0.0f);
}

void TestTickWindows() {
  CHECK(ScaleTickWindow(10, 1.0) == 10);
  CHECK(ScaleTickWindow(15, 1.0) == 15);
  CHECK(ScaleTickWindow(10, 0.25) == 40);
  CHECK(ScaleTickWindow(15, 0.5) == 30);
  CHECK(ScaleTickWindow(10, 2.0) == 5);
  CHECK(ScaleTickWindow(10, 0.0) == 10);
}

void TestFirstGravityStep() {
  const float g30 = -14.0f / 30.0f;
  CHECK(ScaleFirstGravityStep(g30, 0.0f, 1.0) == g30);
  // From the ground the 30 FPS increment, in the air the real one.
  const float g120 = -14.0f / 120.0f;
  CHECK(ScaleFirstGravityStep(g120, 0.0f, 0.25) == g120 / 0.25f);
  CHECK(ScaleFirstGravityStep(g120, -2.5f, 0.25) == g120);
  CHECK(ScaleFirstGravityStep(2 * g30, 0.0f, 2.0) == 2 * g30);
}

void TestRagdollPoseDt() {
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
}

void TestProjectileProbeDt() {
  // The ray reaches as far as one 30 FPS step at 30, 60, 120 and an Unlimited 300 FPS frame.
  for (const double steps : {1.0, 0.5, 0.25, 0.1}) {
    const float dt = float(steps / 30.0);
    CHECK(std::fabs(ScaleProjectileProbeDt(dt, steps) - 1.0f / 30.0f) < 1e-7f);
  }
  CHECK(ScaleProjectileProbeDt(1.0f / 30.0f, 1.0) == 1.0f / 30.0f);
  CHECK(ScaleProjectileProbeDt(2.0f / 30.0f, 2.0) == 2.0f / 30.0f);
  CHECK(ScaleProjectileProbeDt(1.0f / 120.0f, 0.0) == 1.0f / 120.0f);
}

void TestCameraLiftDivisor() {
  CHECK(GuardCameraLiftDivisor(0.5f) == 0.5f);
  CHECK(GuardCameraLiftDivisor(-0.2f) == -0.2f);
  CHECK(GuardCameraLiftDivisor(0.0f) == kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(-0.0f) == -kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(1e-6f) == kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(-1e-6f) == -kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(kMinCameraLift) == kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(-kMinCameraLift) == -kMinCameraLift);
  CHECK(std::isnan(GuardCameraLiftDivisor(NAN)));
  CHECK(std::isinf(GuardCameraLiftDivisor(INFINITY)));
  CHECK(GuardCameraLiftDivisor(2.5f) == 2.5f);
  CHECK(GuardCameraLiftDivisor(-2.5f) == -2.5f);
}

}  // namespace

int main() {
  TestLedgeSnapDown();
  TestGroundSnapDecay();
  TestGroundSnapLerp();
  TestTickWindows();
  TestFirstGravityStep();
  TestRagdollPoseDt();
  TestProjectileProbeDt();
  TestCameraLiftDivisor();
  return check::Finish("character_step_fixes_tests");
}
