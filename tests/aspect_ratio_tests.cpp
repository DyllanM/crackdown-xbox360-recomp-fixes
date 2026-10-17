#include <cmath>
#include <iterator>
#include <string_view>

#include "aspect_ratio.h"
#include "check.h"

using namespace crackdown;

namespace {

bool Same(WindowSize a, WindowSize b) {
  return a.width == b.width && a.height == b.height;
}

WindowSize Scaled(WindowSize size, int scale) {
  return {size.width * scale, size.height * scale};
}

void TestDefaultAndParsing() {
  CHECK(ParseAspectRatio("16:9") == AspectRatio::k16x9);
  CHECK(ParseAspectRatio("") == AspectRatio::k16x9);
  CHECK(ParseAspectRatio("16:9 Original") == AspectRatio::k16x9);
  CHECK(ParseAspectRatio("21") == AspectRatio::k16x9);
  CHECK(ParseAspectRatio("21:9") == AspectRatio::k21x9);
  CHECK(ParseAspectRatio("5:4") == AspectRatio::k5x4);
  for (const AspectRatioInfo& info : kAspectRatios) {
    CHECK(ParseAspectRatio(info.key) == info.aspect);
  }
}

void TestLaunchChoices() {
  const std::string_view labels[] = {"16:9 Original", "16:10", "21:9 Ultrawide", "32:9 Super Ultrawide", "4:3", "5:4"};
  static_assert(sizeof(kAspectRatios) / sizeof(kAspectRatios[0]) == std::size(labels));
  for (size_t i = 0; i < std::size(labels); ++i) {
    CHECK(std::string_view(kAspectRatios[i].label) == labels[i]);
  }
  CHECK(kAspectRatios[0].aspect == AspectRatio::k16x9);
}

void TestSixteenNineOriginalIsNoOp() {
  CHECK(!PinnedVideoMode(AspectRatio::k16x9));
  CHECK(!CameraAspectOverride(AspectRatio::k16x9));
  CHECK(Same(RenderPicture(AspectRatio::k16x9), {1280, 720}));
}

void TestRenderPictures() {
  CHECK(CanvasWidth(AspectRatio::k16x10) == 1152);
  CHECK(CanvasWidth(AspectRatio::k21x9) == 1680);
  CHECK(CanvasWidth(AspectRatio::k32x9) == 2560);
  CHECK(CanvasWidth(AspectRatio::k4x3) == 960);
  CHECK(CanvasWidth(AspectRatio::k5x4) == 896);
  for (const AspectRatioInfo& info : kAspectRatios) {
    CHECK(CanvasWidth(info.aspect) % kCanvasWidthStep == 0);
  }

  CHECK(Same(RenderPicture(AspectRatio::k21x9), {1680, 720}));
  CHECK(Same(Scaled(RenderPicture(AspectRatio::k21x9), 2), {3360, 1440}));
  CHECK(Same(Scaled(RenderPicture(AspectRatio::k21x9), 3), {5040, 2160}));
  CHECK(Same(Scaled(RenderPicture(AspectRatio::k21x9), 4), {6720, 2880}));
  CHECK(Same(Scaled(RenderPicture(AspectRatio::k16x10), 2), {2304, 1440}));
  CHECK(Same(RenderPicture(AspectRatio::k32x9), {2560, 720}));
}

// The picture comes from the format alone. 21:9 windows of 1260x540 and 2520x1080 pin the same 1680x720 picture.
void TestPictureIgnoresWindowSize() {
  for (const AspectWindowSize& size : kWindowSizes) {
    if (size.aspect == AspectRatio::k21x9) {
      CHECK(Same(*PinnedVideoMode(size.aspect), {1680, 720}));
    }
  }
}

void TestCameraAspects() {
  CHECK(*CameraAspectOverride(AspectRatio::k16x10) == 16.0 / 10.0);
  CHECK(*CameraAspectOverride(AspectRatio::k21x9) == 21.0 / 9.0);
  CHECK(*CameraAspectOverride(AspectRatio::k32x9) == 32.0 / 9.0);
  CHECK(*CameraAspectOverride(AspectRatio::k4x3) == 4.0 / 3.0);
  CHECK(std::abs(*CameraAspectOverride(AspectRatio::k5x4) - 5.0 / 4.0) < 0.01);
}

void TestWindowSizeList() {
  int sixteen_nine = 0;
  for (const AspectWindowSize& size : kWindowSizes) {
    CHECK(WindowMatchesAspect({size.width, size.height}, size.aspect));
    sixteen_nine += size.aspect == AspectRatio::k16x9;
  }
  CHECK(sixteen_nine == 4);
  for (const AspectRatioInfo& info : kAspectRatios) {
    CHECK(WindowMatchesAspect(info.default_window, info.aspect));
    int listed = 0;
    for (const AspectWindowSize& size : kWindowSizes) {
      listed += size.aspect == info.aspect;
    }
    CHECK(listed >= 2);
  }
  CHECK(!WindowMatchesAspect({1280, 720}, AspectRatio::k21x9));
  CHECK(!WindowMatchesAspect({0, 720}, AspectRatio::k16x9));
}

void TestWindowSizeForAspect() {
  const auto anyFits = [](WindowSize) { return true; };
  CHECK(Same(WindowSizeForAspect(AspectRatio::k21x9, {2520, 1080}, anyFits), {2520, 1080}));
  CHECK(Same(WindowSizeForAspect(AspectRatio::k16x9, {1600, 900}, anyFits), {1600, 900}));
  CHECK(Same(WindowSizeForAspect(AspectRatio::k21x9, {1280, 720}, anyFits), {1680, 720}));
  CHECK(Same(WindowSizeForAspect(AspectRatio::k16x10, {1920, 1080}, anyFits), {1280, 800}));
  CHECK(Same(WindowSizeForAspect(AspectRatio::k32x9, {1280, 720}, anyFits), {1920, 540}));
  CHECK(Same(WindowSizeForAspect(AspectRatio::k4x3, {1280, 720}, anyFits), {1024, 768}));
  CHECK(Same(WindowSizeForAspect(AspectRatio::k5x4, {1280, 720}, anyFits), {1280, 1024}));

  const auto fitsFullHd = [](WindowSize size) { return size.width < 1920 && size.height < 1080; };
  CHECK(Same(WindowSizeForAspect(AspectRatio::k32x9, {1280, 720}, fitsFullHd), {1600, 450}));
  CHECK(Same(WindowSizeForAspect(AspectRatio::k21x9, {1280, 720}, fitsFullHd), {1680, 720}));

  const auto nothingFits = [](WindowSize) { return false; };
  CHECK(Same(WindowSizeForAspect(AspectRatio::k21x9, {1280, 720}, nothingFits), {1680, 720}));
}

}  // namespace

int main() {
  TestDefaultAndParsing();
  TestLaunchChoices();
  TestSixteenNineOriginalIsNoOp();
  TestRenderPictures();
  TestPictureIgnoresWindowSize();
  TestCameraAspects();
  TestWindowSizeList();
  TestWindowSizeForAspect();
  return check::Finish("aspect_ratio_tests");
}
