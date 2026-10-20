#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace crackdown::nan_guard {

// The camera update writes through the target matrix and the follow state up to +7140.
inline constexpr uint32_t kCameraBytes = 7152;

struct CameraField {
  uint32_t offset;
  const char* name;
};

inline constexpr CameraField kCameraFields[] = {{272, "basis x"},        {288, "basis y"},
                                                {304, "basis z"},        {320, "position"},
                                                {6336, "velocity"},      {6352, "target"},
                                                {6368, "previous target"}, {6384, "listener point"}};

inline constexpr std::size_t kCameraFieldCount = sizeof(kCameraFields) / sizeof(kCameraFields[0]);
static_assert(kCameraFields[kCameraFieldCount - 1].offset + 12 <= kCameraBytes);

inline bool XyzFinite(const float* xyz) {
  return std::isfinite(xyz[0]) && std::isfinite(xyz[1]) && std::isfinite(xyz[2]);
}

}  // namespace crackdown::nan_guard
