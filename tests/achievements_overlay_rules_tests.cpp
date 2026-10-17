#include <cmath>

#include "achievements_overlay_rules.h"
#include "check.h"

using namespace crackdown;

namespace {

constexpr uint16_t kNone = 0;

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void TestOpenOverlayHidesInputAndBClosesOnce() {
  AchievementsOverlayGate gate;
  gate.SetOpen(true);
  auto v = gate.Poll(0, kNone, 0);
  CHECK(v.hide_input && !v.close);
  v = gate.Poll(0, kXinputButtonB, 0);
  CHECK(v.close && v.hide_input);
  v = gate.Poll(0, kXinputButtonB, 0);
  CHECK(!v.close);
}

void TestHeldBAfterCloseIsRemovedUntilReleased() {
  AchievementsOverlayGate gate;
  gate.SetOpen(true);
  gate.Poll(0, kNone, 0);
  gate.Poll(0, kXinputButtonB, 0);
  gate.SetOpen(false);
  auto v = gate.Poll(0, kXinputButtonB, 0);
  CHECK(!v.close && v.clear_b && !v.hide_input);
  v = gate.Poll(0, kNone, 0);
  CHECK(!v.hide_input && !v.clear_b);
  CHECK(!gate.HoldsInput());
}

void TestClosingWithoutBReturnsInputAtOnce() {
  AchievementsOverlayGate gate;
  gate.SetOpen(true);
  gate.Poll(0, kNone, 0);
  gate.SetOpen(false);
  const auto v = gate.Poll(0, kNone, 0);
  CHECK(!v.hide_input && !v.clear_b && !v.close);
  CHECK(!gate.HoldsInput());
}

void TestNewBAfterCloseReachesGame() {
  AchievementsOverlayGate gate;
  gate.SetOpen(true);
  gate.Poll(0, kNone, 0);
  gate.Poll(0, kXinputButtonB, 0);
  gate.SetOpen(false);
  gate.Poll(0, kXinputButtonB, 0);
  gate.Poll(0, kNone, 0);
  const auto v = gate.Poll(0, kXinputButtonB, 0);
  CHECK(!v.clear_b && !v.hide_input && !v.close);
}

void TestBHeldWhenOpeningDoesNotClose() {
  AchievementsOverlayGate gate;
  gate.Poll(0, kXinputButtonB, 0);
  gate.SetOpen(true);
  auto v = gate.Poll(0, kXinputButtonB, 0);
  CHECK(!v.close && v.hide_input);
  gate.Poll(0, kNone, 0);
  v = gate.Poll(0, kXinputButtonB, 0);
  CHECK(v.close);
}

void TestBWhileClosedDoesNothing() {
  AchievementsOverlayGate gate;
  const auto v = gate.Poll(0, kXinputButtonB, 0);
  CHECK(!v.close && !v.hide_input);
}

void TestEveryControllerIsNeutralWhileOpen() {
  AchievementsOverlayGate gate;
  gate.SetOpen(true);
  gate.Poll(1, kNone, 0);
  CHECK(gate.Poll(1, kXinputButtonB, 0).close);
  CHECK(gate.Poll(3, kNone, 20000).hide_input);
}

void TestScrollingFromDpadAndStick() {
  AchievementsOverlayGate gate;
  gate.SetOpen(true);
  CHECK(gate.Poll(0, kXinputDpadDown, 0).scroll == 1.0f);
  CHECK(gate.Poll(0, kXinputDpadUp, 0).scroll == -1.0f);
  CHECK(Near(gate.Poll(0, kNone, 20000).scroll, -12000.0f / 24767.0f));
  CHECK(Near(gate.Poll(0, kNone, -20000).scroll, 12000.0f / 24767.0f));
  CHECK(gate.Poll(0, kNone, 5000).scroll == 0.0f);
  CHECK(gate.Poll(0, kNone, -32768).scroll == 1.0f);
  gate.SetOpen(false);
  CHECK(gate.Poll(0, kXinputDpadDown, 0).scroll == 0.0f);
}

void TestWindowExtentFitsTheViewport() {
  CHECK(Near(FitWindowExtent(1280, 420, 560), 560));
  CHECK(Near(FitWindowExtent(1920, 420, 560), 560));
  CHECK(Near(FitWindowExtent(720, 320, 460), 460));
  CHECK(Near(FitWindowExtent(1080, 320, 460), 460));
  CHECK(Near(FitWindowExtent(600, 420, 560), 420));
  CHECK(Near(FitWindowExtent(360, 320, 460), 320));
  CHECK(Near(FitWindowExtent(300, 320, 460), 268));
}

}  // namespace

int main() {
  TestOpenOverlayHidesInputAndBClosesOnce();
  TestHeldBAfterCloseIsRemovedUntilReleased();
  TestClosingWithoutBReturnsInputAtOnce();
  TestNewBAfterCloseReachesGame();
  TestBHeldWhenOpeningDoesNotClose();
  TestBWhileClosedDoesNothing();
  TestEveryControllerIsNeutralWhileOpen();
  TestScrollingFromDpadAndStick();
  TestWindowExtentFitsTheViewport();
  return check::Finish("achievements_overlay_rules_tests");
}
