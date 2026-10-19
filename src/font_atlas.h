#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Building a sharper font atlas: Crackdown_Font.abc metrics, DDS read/write with a BC3 encoder, and resampling.
// Its own namespace because crackdown::Rect already exists (window_placement.h).
namespace crackdown::font_atlas {

// RGBA8, straight alpha, rows top to bottom.
struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
};

Image MakeImage(uint32_t width, uint32_t height);

// Pixel rectangle [x0, x1) x [y0, y1).
struct Rect {
  uint32_t x0 = 0;
  uint32_t y0 = 0;
  uint32_t x1 = 0;
  uint32_t y1 = 0;

  uint32_t width() const { return x1 - x0; }
  uint32_t height() const { return y1 - y0; }
  bool operator==(const Rect&) const = default;
};

// ---- Crackdown_Font.abc (big-endian) ----

// x0..y1 are atlas pixels (the game divides by the texture size); xoff, width and advance are layout units.
struct GlyphRecord {
  uint16_t x0 = 0;
  uint16_t y0 = 0;
  uint16_t x1 = 0;
  uint16_t y1 = 0;
  int16_t xoff = 0;
  int16_t width = 0;
  int16_t advance = 0;
  uint16_t channel_mask = 0;

  bool operator==(const GlyphRecord&) const = default;
};

// The button glyphs are chars U+0190..U+01B7.
inline constexpr char16_t kFirstButtonChar = 0x0190;
inline constexpr char16_t kLastButtonChar = 0x01B7;

struct FontMetrics {
  std::array<uint8_t, 20> header{};  // u32 version (5) and four f32, kept as raw bytes
  uint16_t max_char = 0;
  std::vector<uint16_t> map;  // max_char + 1 entries: char -> glyph index; 0 is the missing-glyph box (record 0)
  std::vector<GlyphRecord> glyphs;
};

// Empty for anything but a complete, consistent version 5 file: records end exactly at the end of the data, every
// map entry names a record, every rect has x0 <= x1 and y0 <= y1.
std::optional<FontMetrics> ParseAbc(std::span<const uint8_t> bytes);
std::vector<uint8_t> WriteAbc(const FontMetrics& metrics);

// Multiplies every rect by `factor`; layout fields stay. False (and nothing changed) when a coordinate would not fit
// in 16 bits or factor is 0.
bool ScaleGlyphRects(FontMetrics& metrics, uint32_t factor);

// The record `c` maps to (the box record for unmapped chars); null past max_char.
const GlyphRecord* GlyphFor(const FontMetrics& metrics, char16_t c);
inline Rect GlyphRect(const GlyphRecord& glyph) { return {glyph.x0, glyph.y0, glyph.x1, glyph.y1}; }

// Whether every glyph rect lies inside a width x height atlas.
bool GlyphRectsFit(const FontMetrics& metrics, uint32_t width, uint32_t height);

// ---- Block compression ----

enum class BlockFormat { kBc1, kBc2, kBc3 };  // DXT1, DXT3, DXT5

// Decodes one block into 16 RGBA pixels, row-major. BC2/BC3 color is always four-color.
void DecodeBlock(BlockFormat format, const uint8_t* block, uint8_t rgba[64]);
// Encodes 16 RGBA pixels (row-major, straight alpha) as one 16-byte BC3 block.
void EncodeBc3Block(const uint8_t rgba[64], uint8_t block[16]);

// One mip level of blocks, edge blocks padded by repeating the last row and column.
std::vector<uint8_t> EncodeBc3(const Image& image);
std::optional<Image> DecodeBlocks(BlockFormat format, uint32_t width, uint32_t height, std::span<const uint8_t> data);

// ---- DDS (little-endian) ----

// The top level of a DXT1/DXT3/DXT5 texture (FourCC or DX10 header) or a 32-bit uncompressed one with channel masks
// (A8R8G8B8, A8B8G8R8, X8R8G8B8...). Empty for anything else or truncated data. At most 16384 pixels per side.
std::optional<Image> ReadDds(std::span<const uint8_t> bytes);
// A DXT5 texture; with `mips`, the full chain down to 1x1 made with Downsample2x.
std::vector<uint8_t> WriteDdsBc3(const Image& image, bool mips);

// ---- Resampling (alpha-weighted, i.e. on premultiplied color, written back as straight alpha) ----
// Fully transparent results keep the plain average of the source colors instead of turning black.

// Next mip level: 2x2 box filter, each side max(1, side / 2).
Image Downsample2x(const Image& image);

// Catmull-Rom bicubic `factor` times larger, clamped. Empty for factor 0 or a side past 16384.
Image Upscale(const Image& image, uint32_t factor);

// src_rect resampled to out_width x out_height: Catmull-Rom, widened by the ratio when shrinking (antialiased),
// taps outside src_rect dropped. Empty when src_rect is empty or outside src.
Image Resample(const Image& src, Rect src_rect, uint32_t out_width, uint32_t out_height);

// Clears dst_rect to transparent black, then draws src_rect into it as large as fits with its aspect ratio kept,
// centered. False (dst untouched) when a rect is empty or outside its image.
bool PasteFit(Image& dst, Rect dst_rect, const Image& src, Rect src_rect);

}  // namespace crackdown::font_atlas
