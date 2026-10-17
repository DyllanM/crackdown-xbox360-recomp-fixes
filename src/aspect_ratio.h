#pragma once

#include <cstdlib>
#include <optional>
#include <string_view>

namespace crackdown {

enum class AspectRatio { k16x9, k16x10, k21x9, k32x9, k4x3, k5x4 };

// The game draws 720 lines at every format; only the width follows the format.
inline constexpr int kCanvasHeight = 720;
// Canvas widths are rounded to this, the alignment the render targets and resolves need.
inline constexpr int kCanvasWidthStep = 16;

struct WindowSize {
  int width = 0;
  int height = 0;
};

struct AspectRatioInfo {
  AspectRatio aspect;
  const char* key;
  const char* label;
  int ratio_x;
  int ratio_y;
  WindowSize default_window;
};

inline constexpr AspectRatioInfo kAspectRatios[] = {
    {AspectRatio::k16x9, "16:9", "16:9 Original", 16, 9, {1280, 720}},
    {AspectRatio::k16x10, "16:10", "16:10", 16, 10, {1280, 800}},
    {AspectRatio::k21x9, "21:9", "21:9 Ultrawide", 21, 9, {1680, 720}},
    {AspectRatio::k32x9, "32:9", "32:9 Super Ultrawide", 32, 9, {1920, 540}},
    {AspectRatio::k4x3, "4:3", "4:3", 4, 3, {1024, 768}},
    {AspectRatio::k5x4, "5:4", "5:4", 5, 4, {1280, 1024}},
};

struct AspectWindowSize {
  int width;
  int height;
  AspectRatio aspect;
};

// Logical pixels. The launcher lists only the sizes of the selected format that fit its display.
inline constexpr AspectWindowSize kWindowSizes[] = {
    {1280, 720, AspectRatio::k16x9},   {1600, 900, AspectRatio::k16x9},   {1920, 1080, AspectRatio::k16x9},
    {2560, 1440, AspectRatio::k16x9},  {1280, 800, AspectRatio::k16x10},  {1440, 900, AspectRatio::k16x10},
    {1680, 1050, AspectRatio::k16x10}, {1920, 1200, AspectRatio::k16x10}, {1260, 540, AspectRatio::k21x9},
    {1680, 720, AspectRatio::k21x9},   {2100, 900, AspectRatio::k21x9},   {2520, 1080, AspectRatio::k21x9},
    {1600, 450, AspectRatio::k32x9},   {1920, 540, AspectRatio::k32x9},   {2560, 720, AspectRatio::k32x9},
    {3840, 1080, AspectRatio::k32x9},  {1024, 768, AspectRatio::k4x3},    {1280, 960, AspectRatio::k4x3},
    {1600, 1200, AspectRatio::k4x3},   {1000, 800, AspectRatio::k5x4},    {1280, 1024, AspectRatio::k5x4},
};

constexpr const AspectRatioInfo& InfoFor(AspectRatio aspect) {
  for (const AspectRatioInfo& info : kAspectRatios) {
    if (info.aspect == aspect) {
      return info;
    }
  }
  return kAspectRatios[0];
}

// A missing or unknown value is the original 16:9 picture.
constexpr AspectRatio ParseAspectRatio(std::string_view key) {
  for (const AspectRatioInfo& info : kAspectRatios) {
    if (key == info.key) {
      return info.aspect;
    }
  }
  return AspectRatio::k16x9;
}

constexpr int CanvasWidth(AspectRatio aspect) {
  const AspectRatioInfo& info = InfoFor(aspect);
  const int half_step = kCanvasWidthStep / 2;
  return (kCanvasHeight * info.ratio_x + half_step * info.ratio_y) / (kCanvasWidthStep * info.ratio_y) *
         kCanvasWidthStep;
}

// The render resolution at 1x for a format. Render resolution multiplies it, and Internal render resolution scales that.
constexpr WindowSize RenderPicture(AspectRatio aspect) {
  return {CanvasWidth(aspect), kCanvasHeight};
}

// The guest video mode a format pins. 16:9 Original pins none, so the title keeps the window-sized mode it always had.
constexpr std::optional<WindowSize> PinnedVideoMode(AspectRatio aspect) {
  if (aspect == AspectRatio::k16x9) {
    return std::nullopt;
  }
  return RenderPicture(aspect);
}

// The camera aspect of the format's canvas, or nullopt for 16:9 Original.
constexpr std::optional<double> CameraAspectOverride(AspectRatio aspect) {
  if (aspect == AspectRatio::k16x9) {
    return std::nullopt;
  }
  return double(CanvasWidth(aspect)) / kCanvasHeight;
}

// Whether a window has the shape of the format, to within 1%.
constexpr bool WindowMatchesAspect(WindowSize size, AspectRatio aspect) {
  if (size.width <= 0 || size.height <= 0) {
    return false;
  }
  const AspectRatioInfo& info = InfoFor(aspect);
  const long long window = (long long)size.width * info.ratio_y;
  const long long format = (long long)size.height * info.ratio_x;
  return std::llabs(window - format) * 100 <= format;
}

// The current Window size when it already has the format's shape, else the format's default when it fits, else the
// largest listed size of the format that fits. `fits` says whether a size fits the selected display.
template <typename Fits>
WindowSize WindowSizeForAspect(AspectRatio aspect, WindowSize current, Fits fits) {
  if (WindowMatchesAspect(current, aspect)) {
    return current;
  }
  const WindowSize preferred = InfoFor(aspect).default_window;
  if (fits(preferred)) {
    return preferred;
  }
  WindowSize largest;
  for (const AspectWindowSize& size : kWindowSizes) {
    const WindowSize listed{size.width, size.height};
    if (size.aspect == aspect && listed.width > largest.width && fits(listed)) {
      largest = listed;
    }
  }
  return largest.width > 0 ? largest : preferred;
}

}  // namespace crackdown
