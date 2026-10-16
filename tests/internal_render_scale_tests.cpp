#include "check.h"
#include "internal_render_scale.h"

using namespace crackdown;

namespace {

void TestChoices() {
  CHECK(NormalizeInternalRenderScale(100) == 100);
  CHECK(NormalizeInternalRenderScale(75) == 75);
  CHECK(NormalizeInternalRenderScale(50) == 50);
  CHECK(NormalizeInternalRenderScale(0) == 100);
  CHECK(NormalizeInternalRenderScale(60) == 100);
  CHECK(NormalizeInternalRenderScale(200) == 100);
}

// Matches TextureCache::GetConfigInternalRenderScale in patch 0047 at every render resolution the launcher offers.
void TestSplit() {
  for (int scale = 1; scale <= 4; ++scale) {
    const InternalRenderSplit native = SplitInternalRenderScale(scale, 100);
    CHECK(native.host_quarters == 4 * scale && native.resolve_scale == scale);
    const InternalRenderSplit performance = SplitInternalRenderScale(scale, 75);
    CHECK(performance.host_quarters == 3 * scale && performance.resolve_scale == (3 * scale + 3) / 4);
    const InternalRenderSplit low = SplitInternalRenderScale(scale, 50);
    CHECK(low.host_quarters == 2 * scale && low.resolve_scale == (2 * scale + 3) / 4);
    CHECK(low.resolve_scale >= 1 && low.resolve_scale <= scale);
  }
}

void TestResolution() {
  CHECK(InternalRenderWidth(1, 100) == 1280 && InternalRenderHeight(1, 100) == 720);
  CHECK(InternalRenderWidth(2, 75) == 1920 && InternalRenderHeight(2, 75) == 1080);
  CHECK(InternalRenderWidth(3, 50) == 1920 && InternalRenderHeight(3, 50) == 1080);
  CHECK(InternalRenderWidth(4, 75) == 3840 && InternalRenderHeight(4, 75) == 2160);
  CHECK(DescribeInternalRenderResolution(2, 75) == "Internal render resolution: 75% 1920x1080 -> 2560x1440");
  CHECK(DescribeInternalRenderResolution(1, 33) == "Internal render resolution: 100% 1280x720 -> 1280x720");
}

void TestRenderTargetPath() {
  CHECK(!RequiredRenderTargetPath(100, ""));
  CHECK(RequiredRenderTargetPath(75, "") == "rtv");
  CHECK(RequiredRenderTargetPath(50, "") == "rtv");
  CHECK(!RequiredRenderTargetPath(75, "rov"));
  CHECK(!RequiredRenderTargetPath(42, ""));
}

}  // namespace

int main() {
  TestChoices();
  TestSplit();
  TestResolution();
  TestRenderTargetPath();
  return check::Finish("internal_render_scale_tests");
}
