#include <cmath>
#include <limits>

#include "check.h"
#include "mouse_look.h"

using namespace crackdown::mouse_look;

namespace {

bool Near(double a, double b) {
  return std::fabs(a - b) <= 1e-9;
}

void TestZero() {
  const Turn turn = TurnFor({0.0, 0.0}, 1.0, 1.0, false);
  CHECK(!turn.any());
  CHECK(turn.yaw == 0.0);
  CHECK(turn.pitch == 0.0);
}

void TestHorizontalSign() {
  const Turn right = TurnFor({100.0, 0.0}, 1.0, 1.0, false);
  const Turn left = TurnFor({-100.0, 0.0}, 1.0, 1.0, false);
  // Crackdown yaw decreases when the camera turns right.
  CHECK(right.yaw < 0.0);
  CHECK(left.yaw > 0.0);
  CHECK(Near(right.yaw, -left.yaw));
  CHECK(right.pitch == 0.0);
}

void TestVerticalSign() {
  const Turn down = TurnFor({0.0, 100.0}, 1.0, 1.0, false);
  const Turn up = TurnFor({0.0, -100.0}, 1.0, 1.0, false);
  CHECK(down.pitch < 0.0);
  CHECK(up.pitch > 0.0);
  CHECK(Near(down.pitch, -up.pitch));
  CHECK(down.yaw == 0.0);
}

void TestSensitivityScaling() {
  const Delta delta{200.0, -150.0};
  const Turn low = TurnFor(delta, 0.10, 1.0, false);
  const Turn one = TurnFor(delta, 1.00, 1.0, false);
  const Turn high = TurnFor(delta, 10.0, 1.0, false);
  CHECK(Near(one.yaw, -200.0 * kRadiansPerCount));
  CHECK(Near(low.yaw, one.yaw * 0.10));
  CHECK(Near(high.yaw, one.yaw * 10.0));
  CHECK(Near(high.pitch, one.pitch * 10.0));
}

void TestInvalidSensitivityFallsBack() {
  CHECK(ClampSensitivity(0.0) == 1.0);
  CHECK(ClampSensitivity(-3.0) == 1.0);
  CHECK(ClampSensitivity(std::nan("")) == 1.0);
  CHECK(ClampSensitivity(0.001) == kMinSensitivity);
  CHECK(ClampSensitivity(1000.0) == kMaxSensitivity);
}

void TestDpiScale() {
  const Turn one = TurnFor({100.0, 0.0}, 1.0, 1.0, false);
  const Turn hidpi = TurnFor({100.0, 0.0}, 1.0, 2.0, false);
  CHECK(Near(hidpi.yaw, one.yaw / 2.0));
}

void TestInversion() {
  const Delta delta{0.0, 100.0};
  const Turn normal = TurnFor(delta, 1.0, 1.0, false);
  const Turn inverted = TurnFor(delta, 1.0, 1.0, true);
  CHECK(Near(inverted.pitch, -normal.pitch));
  CHECK(inverted.yaw == normal.yaw);
}

void TestAccumulationDrain() {
  Accumulator accumulator;
  accumulator.Add(3.0, -2.0);
  accumulator.Add(4.0, 5.0);
  CHECK(!accumulator.empty());
  const Delta taken = accumulator.Take();
  CHECK(Near(taken.x, 7.0));
  CHECK(Near(taken.y, 3.0));
  CHECK(accumulator.empty());
  const Delta again = accumulator.Take();
  CHECK(again.x == 0.0 && again.y == 0.0);
}

void TestClearDropsMovement() {
  Accumulator accumulator;
  accumulator.Add(50.0, -50.0);
  accumulator.Clear();
  const Delta taken = accumulator.Take();
  CHECK(taken.x == 0.0 && taken.y == 0.0);
}

void TestFiniteExtremes() {
  const Turn turn = TurnFor({1.0e9, -1.0e9}, 10.0, 1.0, false);
  CHECK(std::isfinite(turn.yaw));
  CHECK(std::isfinite(turn.pitch));
  const Turn nan = TurnFor({std::nan(""), 1.0}, 1.0, 1.0, false);
  CHECK(!nan.any());
  Accumulator accumulator;
  accumulator.Add(std::nan(""), std::numeric_limits<double>::infinity());
  CHECK(accumulator.empty());
}

void TestCapturePolicy() {
  bool captured = false;
  CHECK(!DirectLookAllowed(true, true, false, captured));
  CHECK(CanAcquireCapture(true, true, false));

  captured = CanAcquireCapture(true, true, false);
  CHECK(captured);
  CHECK(DirectLookAllowed(true, true, false, captured));

  captured = false;  // controller activity, overlay or focus loss
  CHECK(!DirectLookAllowed(true, true, false, captured));
  CHECK(!CanAcquireCapture(true, true, true));
  CHECK(!CanAcquireCapture(true, false, false));
  CHECK(!CanAcquireCapture(false, true, false));

  CHECK(!DirectLookAllowed(true, true, false, captured));
  captured = CanAcquireCapture(true, true, false);
  CHECK(captured);
  CHECK(DirectLookAllowed(true, true, false, captured));
}

}  // namespace

int main() {
  TestZero();
  TestHorizontalSign();
  TestVerticalSign();
  TestSensitivityScaling();
  TestInvalidSensitivityFallsBack();
  TestDpiScale();
  TestInversion();
  TestAccumulationDrain();
  TestClearDropsMovement();
  TestFiniteExtremes();
  TestCapturePolicy();
  return check::Finish("mouse_look_tests");
}
