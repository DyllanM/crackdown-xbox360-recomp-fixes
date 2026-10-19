#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "font_atlas.h"

namespace crackdown::font_atlas {

// The Latin atlases (1024x2048, shared by the eleven Latin languages) hold plain white Arial at 20 px per em, the
// baseline 22 px below the top of each 28 px cell, and the pen -xoff px left of a glyph's rect.
inline constexpr float kAtlasEmPixels = 20.0f;
inline constexpr float kAtlasBaseline = 22.0f;
inline constexpr float kAtlasPenShift = 1.0f;
inline constexpr uint32_t kLatinAtlasWidth = 1024;
inline constexpr uint32_t kLatinAtlasHeight = 2048;
inline constexpr uint32_t kGlyphPadding = 2;

// The first of these that exists: Arial on Windows, Liberation Sans (metric-compatible with Arial) on Linux.
std::optional<std::filesystem::path> FindArialCompatibleFont();

// Redraws every text glyph of a Latin atlas already upscaled by `scale` (metrics' rects scaled too) from the TrueType
// font, so text is sharp instead of interpolated. Each redrawn glyph gets a new rect in the atlas's free rows, sized to
// its bitmap plus kGlyphPadding, with xoff and width (the quad) adjusted; advances, and so layout, are unchanged.
// Button glyphs and characters the font lacks keep their pixels. The number of glyphs redrawn, 0 when the font cannot
// be read.
uint32_t RerenderTextGlyphs(Image& atlas, FontMetrics& metrics, uint32_t scale, std::span<const uint8_t> ttf);

}  // namespace crackdown::font_atlas
