#include <cmath>
#include <limits>

#include "check.h"
#include "nan_guard_rules.h"

using namespace crackdown::nan_guard;

int main() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  CHECK(kCameraBytes == 7152);
  CHECK(kCameraFieldCount == 8);
  CHECK(kCameraFields[0].offset == 272 && std::string_view(kCameraFields[0].name) == "basis x");
  CHECK(kCameraFields[1].offset == 288 && std::string_view(kCameraFields[1].name) == "basis y");
  CHECK(kCameraFields[2].offset == 304 && std::string_view(kCameraFields[2].name) == "basis z");
  CHECK(kCameraFields[3].offset == 320 && std::string_view(kCameraFields[3].name) == "position");
  CHECK(kCameraFields[4].offset == 6336 && std::string_view(kCameraFields[4].name) == "velocity");
  CHECK(kCameraFields[5].offset == 6352 && std::string_view(kCameraFields[5].name) == "target");
  CHECK(kCameraFields[6].offset == 6368 && std::string_view(kCameraFields[6].name) == "previous target");
  CHECK(kCameraFields[7].offset == 6384 && std::string_view(kCameraFields[7].name) == "listener point");
  CHECK(kCameraFields[kCameraFieldCount - 1].offset + 12 <= kCameraBytes);

  const float finite[4] = {1.0f, -2.0f, 3.5f, nan};
  CHECK(XyzFinite(finite));
  for (int lane = 0; lane < 3; ++lane) {
    float bad[4] = {1.0f, -2.0f, 3.5f, 0.0f};
    bad[lane] = nan;
    CHECK(!XyzFinite(bad));
    bad[lane] = inf;
    CHECK(!XyzFinite(bad));
  }

  for (const CameraField& field : kCameraFields) {
    float bad[3] = {1.0f, -2.0f, 3.5f};
    bad[0] = nan;
    CHECK(!XyzFinite(bad));
    CHECK(field.name != nullptr);
  }

  return check::Finish("nan_guard_rules_tests");
}
