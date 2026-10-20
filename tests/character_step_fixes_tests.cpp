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

  return check::Finish("character_step_fixes_tests");
}
