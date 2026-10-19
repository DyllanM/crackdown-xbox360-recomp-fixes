#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace crackdown {

inline constexpr int kBaseDpi = 96;

inline float DpiScaleFromDpi(std::uint32_t dpi) {
  return dpi ? float(dpi) / float(kBaseDpi) : 1.0f;
}

inline float ValidDpiScale(float scale) {
  return scale > 0.0f ? scale : 1.0f;
}

struct DpiWindowSize {
  int width = 0;
  int height = 0;

  bool operator==(const DpiWindowSize&) const = default;
};

// The window size is logical; the work area is in physical screen pixels.
inline DpiWindowSize FitWindowToWorkArea(int width, int height, int work_width, int work_height, float scale,
                                         double max_share) {
  const float valid_scale = ValidDpiScale(scale);
  const int max_width = int(double(work_width) * max_share / double(valid_scale));
  const int max_height = int(double(work_height) * max_share / double(valid_scale));
  return {std::min(width, max_width), std::min(height, max_height)};
}

}  // namespace crackdown
