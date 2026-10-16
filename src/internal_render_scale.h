#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace crackdown {

// The guest video mode is pinned to this in crackdown.toml; resolution_scale multiplies it.
inline constexpr int kPresentedWidth = 1280;
inline constexpr int kPresentedHeight = 720;

inline constexpr int kInternalRenderScaleNative = 100;
inline constexpr int kInternalRenderScalePerformance = 75;
inline constexpr int kInternalRenderScaleLowEnd = 50;

// The launcher's Internal render resolution (internal_render_scale), in percent of the render resolution. Anything
// but the two reduced choices draws at 100%.
inline int NormalizeInternalRenderScale(int percent) {
  return percent == kInternalRenderScalePerformance || percent == kInternalRenderScaleLowEnd
             ? percent
             : kInternalRenderScaleNative;
}

// How ReXGlue splits the render resolution and the internal render scale (TextureCache::GetConfigInternalRenderScale):
// the host render targets are host_quarters / 4 of the guest picture, the EDRAM, resolves and textures keep the
// whole scale above it.
struct InternalRenderSplit {
  int host_quarters;
  int resolve_scale;
};

inline InternalRenderSplit SplitInternalRenderScale(int resolution_scale, int percent) {
  const int host_quarters = resolution_scale * (NormalizeInternalRenderScale(percent) / 25);
  return {host_quarters, (host_quarters + 3) / 4};
}

// The render_target_path_d3d12 value a reduced scale needs; none at 100% or when the player chose a path.
inline std::optional<std::string_view> RequiredRenderTargetPath(int percent, std::string_view configured) {
  if (NormalizeInternalRenderScale(percent) == kInternalRenderScaleNative || !configured.empty()) {
    return std::nullopt;
  }
  return "rtv";
}

inline int InternalRenderWidth(int resolution_scale, int percent) {
  return kPresentedWidth * resolution_scale * NormalizeInternalRenderScale(percent) / 100;
}

inline int InternalRenderHeight(int resolution_scale, int percent) {
  return kPresentedHeight * resolution_scale * NormalizeInternalRenderScale(percent) / 100;
}

inline std::string DescribeInternalRenderResolution(int resolution_scale, int percent) {
  percent = NormalizeInternalRenderScale(percent);
  return "Internal render resolution: " + std::to_string(percent) + "% " +
         std::to_string(InternalRenderWidth(resolution_scale, percent)) + "x" +
         std::to_string(InternalRenderHeight(resolution_scale, percent)) + " -> " +
         std::to_string(kPresentedWidth * resolution_scale) + "x" +
         std::to_string(kPresentedHeight * resolution_scale);
}

}  // namespace crackdown
