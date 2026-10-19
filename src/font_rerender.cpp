#include "font_rerender.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// A private copy of stb_truetype (shipped with the SDK's Dear ImGui), so nothing clashes with ImGui's own.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <imstb_truetype.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace crackdown::font_atlas {

std::optional<std::filesystem::path> FindArialCompatibleFont() {
  std::vector<std::filesystem::path> candidates;
#if defined(_WIN32)
  wchar_t windows[MAX_PATH];
  if (const UINT length = GetWindowsDirectoryW(windows, MAX_PATH); length && length < MAX_PATH) {
    candidates.push_back(std::filesystem::path(windows) / "Fonts" / "arial.ttf");
  }
#else
  for (const char* path : {"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                           "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
                           "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
                           "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                           "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
                           "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf"}) {
    candidates.emplace_back(path);
  }
#endif
  for (const auto& path : candidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
      return path;
    }
  }
  return std::nullopt;
}

namespace {

bool HasInk(const Image& atlas, const GlyphRecord& glyph) {
  constexpr uint8_t kInkAlpha = 32;
  if (glyph.x1 > atlas.width || glyph.y1 > atlas.height) {
    return false;
  }
  for (uint32_t y = glyph.y0; y < glyph.y1; ++y) {
    for (uint32_t x = glyph.x0; x < glyph.x1; ++x) {
      if (atlas.rgba[(size_t(y) * atlas.width + x) * 4 + 3] >= kInkAlpha) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

uint32_t RerenderTextGlyphs(Image& atlas, FontMetrics& metrics, uint32_t scale, std::span<const uint8_t> ttf) {
  stbtt_fontinfo font;
  if (scale == 0 || ttf.empty() || !stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
    return 0;
  }
  const float pixel_scale = stbtt_ScaleForMappingEmToPixels(&font, kAtlasEmPixels * float(scale));
  const int baseline = int(std::lround(kAtlasBaseline * float(scale)));
  const int pen_shift = int(std::lround(kAtlasPenShift * float(scale)));
  const int unit = int(scale);

  // New rects go on shelves below everything the atlas already uses.
  uint32_t top = 0;
  for (const GlyphRecord& glyph : metrics.glyphs) {
    top = std::max<uint32_t>(top, glyph.y1);
  }
  int shelf_x = int(kGlyphPadding);
  int shelf_y = int(top + kGlyphPadding);

  std::vector<bool> done(metrics.glyphs.size(), false);
  done[0] = true;  // the missing-glyph box
  uint32_t drawn = 0;
  std::vector<uint8_t> coverage;
  for (uint32_t c = 0x21; c <= metrics.max_char && c < metrics.map.size(); ++c) {
    const uint16_t index = metrics.map[c];
    if (index >= metrics.glyphs.size() || done[index] || (c >= kFirstButtonChar && c <= kLastButtonChar)) {
      continue;
    }
    const int font_glyph = stbtt_FindGlyphIndex(&font, int(c));
    if (!font_glyph) {
      continue;
    }
    done[index] = true;
    GlyphRecord& glyph = metrics.glyphs[index];
    const int height = int(glyph.y1) - int(glyph.y0);  // the cell: the game draws every glyph this tall
    // A blank record is the space, shared by characters the original font left out: it stays blank.
    if (!HasInk(atlas, glyph)) {
      continue;
    }
    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    stbtt_GetGlyphBitmapBox(&font, font_glyph, pixel_scale, pixel_scale, &bx0, &by0, &bx1, &by1);
    const int bw = bx1 - bx0;
    const int bh = by1 - by0;
    if (height <= 0 || bw <= 0 || bh <= 0) {
      continue;
    }
    // The rect, in whole layout units relative to the pen, holds the bitmap plus padding, so nothing is clipped. The
    // quad follows xoff and width; the pen advance, and so the text layout, does not change.
    const int left = pen_shift + bx0 - int(kGlyphPadding);
    const int right = pen_shift + bx1 + int(kGlyphPadding);
    const int xoff = int(std::floor(float(left) / float(unit)));
    const int units = int(std::ceil(float(right) / float(unit))) - xoff;
    const int width = units * unit;
    if (shelf_x + width + int(kGlyphPadding) > int(atlas.width)) {
      shelf_x = int(kGlyphPadding);
      shelf_y += height + int(kGlyphPadding);
    }
    if (shelf_y + height > int(atlas.height)) {
      break;  // no room left; the remaining glyphs keep their interpolated pixels
    }
    coverage.assign(size_t(bw) * size_t(bh), 0);
    stbtt_MakeGlyphBitmap(&font, coverage.data(), bw, bh, bw, pixel_scale, pixel_scale, font_glyph);

    // Transparent white (no dark fringe when filtered), then the coverage as alpha.
    for (int y = 0; y < height; ++y) {
      uint8_t* row = atlas.rgba.data() + (size_t(shelf_y + y) * atlas.width + size_t(shelf_x)) * 4;
      for (int x = 0; x < width; ++x) {
        row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
        row[x * 4 + 3] = 0;
      }
    }
    const int origin_x = pen_shift + bx0 - xoff * unit;
    const int origin_y = baseline + by0;
    for (int y = 0; y < bh; ++y) {
      const int ty = origin_y + y;
      if (ty < 0 || ty >= height) {
        continue;
      }
      for (int x = 0; x < bw; ++x) {
        const int tx = origin_x + x;
        if (tx < 0 || tx >= width) {
          continue;
        }
        const size_t texel = size_t(shelf_y + ty) * atlas.width + size_t(shelf_x + tx);
        atlas.rgba[texel * 4 + 3] = coverage[size_t(y) * size_t(bw) + size_t(x)];
      }
    }
    glyph.x0 = uint16_t(shelf_x);
    glyph.y0 = uint16_t(shelf_y);
    glyph.x1 = uint16_t(shelf_x + width);
    glyph.y1 = uint16_t(shelf_y + height);
    glyph.xoff = int16_t(xoff);
    glyph.width = int16_t(units);
    shelf_x += width + int(kGlyphPadding);
    ++drawn;
  }
  return drawn;
}

}  // namespace crackdown::font_atlas
