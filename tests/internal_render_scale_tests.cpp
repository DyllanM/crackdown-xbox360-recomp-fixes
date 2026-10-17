#include "aspect_ratio.h"
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

// 16:9 Original keeps the pre-feature 1280x720 picture at every Internal render resolution.
void TestSixteenNineResolution() {
  const WindowSize picture = RenderPicture(AspectRatio::k16x9);
  CHECK(InternalRenderWidth(picture, 1, 100) == 1280 && InternalRenderHeight(picture, 1, 100) == 720);
  CHECK(InternalRenderWidth(picture, 2, 75) == 1920 && InternalRenderHeight(picture, 2, 75) == 1080);
  CHECK(InternalRenderWidth(picture, 3, 50) == 1920 && InternalRenderHeight(picture, 3, 50) == 1080);
  CHECK(InternalRenderWidth(picture, 4, 75) == 3840 && InternalRenderHeight(picture, 4, 75) == 2160);
  CHECK(DescribeInternalRenderResolution(picture, 2, 75) ==
        "Internal render resolution: 75% 1920x1080 -> 2560x1440");
  CHECK(DescribeInternalRenderResolution(picture, 1, 33) ==
        "Internal render resolution: 100% 1280x720 -> 1280x720");
}

// The internal sizes apply to the target picture of the format after its Render resolution multiplier.
void TestUltrawideTargets() {
  const WindowSize twentyOneNine = RenderPicture(AspectRatio::k21x9);
  CHECK(InternalRenderWidth(twentyOneNine, 1, 75) == 1260 && InternalRenderHeight(twentyOneNine, 1, 75) == 540);
  CHECK(InternalRenderWidth(twentyOneNine, 1, 50) == 840 && InternalRenderHeight(twentyOneNine, 1, 50) == 360);
  CHECK(InternalRenderWidth(twentyOneNine, 2, 75) == 2520 && InternalRenderHeight(twentyOneNine, 2, 75) == 1080);
  CHECK(DescribeInternalRenderResolution(twentyOneNine, 2, 75) ==
        "Internal render resolution: 75% 2520x1080 -> 3360x1440");
}

// Each format keeps its shape at every Internal render resolution and Render resolution. The canvas widths are
// multiples of 16, so the reduced sizes are whole pixels.
void TestAspectRatioPreserved() {
  const AspectRatio formats[] = {AspectRatio::k16x9, AspectRatio::k16x10, AspectRatio::k21x9,
                                 AspectRatio::k32x9, AspectRatio::k4x3,   AspectRatio::k5x4};
  for (AspectRatio format : formats) {
    const WindowSize picture = RenderPicture(format);
    for (int scale = 1; scale <= 4; ++scale) {
      for (int percent : {100, 75, 50}) {
        const int width = InternalRenderWidth(picture, scale, percent);
        const int height = InternalRenderHeight(picture, scale, percent);
        CHECK(width * picture.height == height * picture.width);
      }
    }
  }
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
  TestSixteenNineResolution();
  TestUltrawideTargets();
  TestAspectRatioPreserved();
  TestRenderTargetPath();
  return check::Finish("internal_render_scale_tests");
}
