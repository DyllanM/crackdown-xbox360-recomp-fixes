#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "check.h"
#include "font_atlas.h"

using namespace crackdown::font_atlas;

namespace {

void PutBe16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(uint8_t(value >> 8));
  out.push_back(uint8_t(value));
}

void PutBe32(std::vector<uint8_t>& out, uint32_t value) {
  PutBe16(out, uint16_t(value >> 16));
  PutBe16(out, uint16_t(value));
}

void PutLe32(std::vector<uint8_t>& out, size_t offset, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out[offset + i] = uint8_t(value >> (8 * i));
  }
}

uint32_t GetLe32(const std::vector<uint8_t>& bytes, size_t offset) {
  return uint32_t(bytes[offset]) | uint32_t(bytes[offset + 1]) << 8 | uint32_t(bytes[offset + 2]) << 16 |
         uint32_t(bytes[offset + 3]) << 24;
}

// Version 5, the game's 30/0/0/30 floats, chars up to the last button; 'A' -> 1, U+0190 -> 2, record 0 the box.
std::vector<uint8_t> SyntheticAbc() {
  std::vector<uint8_t> out;
  PutBe32(out, 5);
  for (uint32_t f : {0x41F00000u, 0u, 0u, 0x41F00000u}) {
    PutBe32(out, f);
  }
  PutBe16(out, kLastButtonChar);
  for (uint32_t c = 0; c <= kLastButtonChar; ++c) {
    PutBe16(out, c == 'A' ? 1 : c == kFirstButtonChar ? 2 : 0);
  }
  PutBe32(out, 3);
  const uint16_t records[3][8] = {
      {3, 1, 24, 29, 2, 21, 23, 0},
      {389, 1, 404, 29, 0xFFFF, 15, 14, 0x0F},
      {643, 59, 671, 87, 0, 28, 28, 0},
  };
  for (const auto& record : records) {
    for (uint16_t value : record) {
      PutBe16(out, value);
    }
  }
  return out;
}

Image Filled(uint32_t width, uint32_t height, const uint8_t (&rgba)[4]) {
  Image image = MakeImage(width, height);
  for (size_t i = 0; i < image.rgba.size(); i += 4) {
    std::memcpy(image.rgba.data() + i, rgba, 4);
  }
  return image;
}

const uint8_t* Pixel(const Image& image, uint32_t x, uint32_t y) {
  return image.rgba.data() + (size_t(y) * image.width + x) * 4;
}

bool PixelIs(const Image& image, uint32_t x, uint32_t y, const uint8_t (&rgba)[4]) {
  return std::memcmp(Pixel(image, x, y), rgba, 4) == 0;
}

int MaxError(const Image& a, const Image& b, int first_channel, int last_channel) {
  int worst = 0;
  for (size_t i = 0; i < a.rgba.size(); i += 4) {
    for (int c = first_channel; c <= last_channel; ++c) {
      worst = std::max(worst, std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c])));
    }
  }
  return worst;
}

Image Bc3RoundTrip(const Image& image) {
  const std::optional<Image> decoded = DecodeBlocks(BlockFormat::kBc3, image.width, image.height, EncodeBc3(image));
  return decoded ? *decoded : Image{};
}

void TestAbcRoundTrip() {
  const std::vector<uint8_t> bytes = SyntheticAbc();
  const std::optional<FontMetrics> metrics = ParseAbc(bytes);
  CHECK(metrics.has_value());
  if (!metrics) {
    return;
  }
  CHECK(metrics->max_char == kLastButtonChar);
  CHECK(metrics->map.size() == size_t(kLastButtonChar) + 1);
  CHECK(metrics->glyphs.size() == 3);
  CHECK(metrics->glyphs[1].xoff == -1 && metrics->glyphs[1].channel_mask == 0x0F);
  CHECK(WriteAbc(*metrics) == bytes);

  CHECK(GlyphFor(*metrics, u'A') == &metrics->glyphs[1]);
  CHECK(GlyphFor(*metrics, kFirstButtonChar) == &metrics->glyphs[2]);
  CHECK(GlyphFor(*metrics, u'B') == &metrics->glyphs[0]);
  CHECK(GlyphFor(*metrics, char16_t(kLastButtonChar + 1)) == nullptr);
  CHECK((GlyphRect(metrics->glyphs[2]) == Rect{643, 59, 671, 87}));
  CHECK(GlyphRectsFit(*metrics, 1024, 2048));
  CHECK(!GlyphRectsFit(*metrics, 670, 2048));
  CHECK(!GlyphRectsFit(*metrics, 1024, 86));
}

void TestScaleGlyphRects() {
  FontMetrics metrics = *ParseAbc(SyntheticAbc());
  const FontMetrics original = metrics;
  CHECK(ScaleGlyphRects(metrics, 2));
  for (size_t i = 0; i < metrics.glyphs.size(); ++i) {
    const GlyphRecord& a = original.glyphs[i];
    const GlyphRecord& b = metrics.glyphs[i];
    CHECK(b.x0 == a.x0 * 2 && b.y0 == a.y0 * 2 && b.x1 == a.x1 * 2 && b.y1 == a.y1 * 2);
    CHECK(b.xoff == a.xoff && b.width == a.width && b.advance == a.advance && b.channel_mask == a.channel_mask);
  }
  CHECK(metrics.map == original.map && metrics.header == original.header);
  CHECK(GlyphRectsFit(metrics, 2048, 4096));

  const FontMetrics doubled = metrics;
  CHECK(!ScaleGlyphRects(metrics, 100));  // 1342 * 100 does not fit in 16 bits
  CHECK(!ScaleGlyphRects(metrics, 0));
  CHECK(metrics.glyphs == doubled.glyphs);
}

void TestAbcRejectsBadInput() {
  const std::vector<uint8_t> bytes = SyntheticAbc();
  bool all_truncations_rejected = true;
  for (size_t size = 0; size < bytes.size(); ++size) {
    all_truncations_rejected &= !ParseAbc(std::span(bytes.data(), size)).has_value();
  }
  CHECK(all_truncations_rejected);

  std::vector<uint8_t> longer = bytes;
  longer.push_back(0);
  CHECK(!ParseAbc(longer));

  std::vector<uint8_t> version = bytes;
  version[3] = 4;
  CHECK(!ParseAbc(version));

  const size_t count_offset = 0x16 + 2 * (size_t(kLastButtonChar) + 1);
  std::vector<uint8_t> bad_index = bytes;
  bad_index[0x16 + 2 * 'A' + 1] = 3;  // only records 0..2 exist
  CHECK(!ParseAbc(bad_index));

  std::vector<uint8_t> huge_count = bytes;
  huge_count[count_offset] = 0x7F;
  CHECK(!ParseAbc(huge_count));

  std::vector<uint8_t> no_glyphs(bytes.begin(), bytes.begin() + count_offset);
  PutBe32(no_glyphs, 0);
  std::fill(no_glyphs.begin() + 0x16, no_glyphs.end() - 4, uint8_t(0));
  CHECK(!ParseAbc(no_glyphs));

  std::vector<uint8_t> inverted = bytes;
  const size_t record1 = count_offset + 4 + 16;
  inverted[record1 + 4] = 0;  // x1 = 0x94 < x0
  CHECK(!ParseAbc(inverted));

  // max_char 0xFFFF with the map cut short.
  std::vector<uint8_t> short_map(bytes.begin(), bytes.begin() + 0x16);
  short_map[0x14] = short_map[0x15] = 0xFF;
  short_map.resize(0x16 + 2 * 65536 + 2, 0);
  CHECK(!ParseAbc(short_map));
}

void TestDecodeBc1() {
  // c0 > c1: four colors. Pixels 0..3 take entries 0..3.
  const uint8_t four[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x00, 0x00, 0x00};
  uint8_t rgba[64];
  DecodeBlock(BlockFormat::kBc1, four, rgba);
  const uint8_t expected[4][4] = {{255, 0, 0, 255}, {0, 0, 255, 255}, {170, 0, 85, 255}, {85, 0, 170, 255}};
  for (int i = 0; i < 4; ++i) {
    CHECK(std::memcmp(rgba + i * 4, expected[i], 4) == 0);
  }
  CHECK(std::memcmp(rgba + 4 * 4, expected[0], 4) == 0);

  // c0 <= c1: three colors and transparent black.
  const uint8_t three[8] = {0x1F, 0x00, 0x00, 0xF8, 0xE4, 0x00, 0x00, 0x00};
  DecodeBlock(BlockFormat::kBc1, three, rgba);
  const uint8_t expected_three[4][4] = {{0, 0, 255, 255}, {255, 0, 0, 255}, {128, 0, 128, 255}, {0, 0, 0, 0}};
  for (int i = 0; i < 4; ++i) {
    CHECK(std::memcmp(rgba + i * 4, expected_three[i], 4) == 0);
  }
}

void TestDecodeBc2() {
  const uint8_t block[16] = {0x10, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
                             0x1F, 0x00, 0x00, 0xF8, 0xE4, 0x00, 0x00, 0x00};
  uint8_t rgba[64];
  DecodeBlock(BlockFormat::kBc2, block, rgba);
  CHECK(rgba[3] == 0 && rgba[7] == 17 && rgba[11] == 34 && rgba[15] == 51);
  CHECK(rgba[14 * 4 + 3] == 0 && rgba[15 * 4 + 3] == 255);
  // Color is four-color even with c0 <= c1.
  const uint8_t third[3] = {85, 0, 170};
  const uint8_t two_thirds[3] = {170, 0, 85};
  CHECK(std::memcmp(rgba + 8, third, 3) == 0);
  CHECK(std::memcmp(rgba + 12, two_thirds, 3) == 0);
}

void TestDecodeBc3() {
  // a0 > a1: eight values; pixels 0..3 take entries 0, 1, 2, 7.
  uint8_t block[16] = {255, 0, 0x88, 0x0E, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
  uint8_t rgba[64];
  DecodeBlock(BlockFormat::kBc3, block, rgba);
  CHECK(rgba[3] == 255 && rgba[7] == 0 && rgba[11] == 219 && rgba[15] == 36);
  CHECK(rgba[0] == 255 && rgba[1] == 255 && rgba[2] == 255);

  // a0 <= a1: six values plus 0 and 255; pixels 0..3 take entries 2, 5, 6, 7.
  const uint64_t bits = 2 | 5 << 3 | 6 << 6 | 7 << 9;
  block[0] = 40;
  block[1] = 140;
  for (int i = 0; i < 6; ++i) {
    block[2 + i] = uint8_t(bits >> (8 * i));
  }
  DecodeBlock(BlockFormat::kBc3, block, rgba);
  CHECK(rgba[3] == 60 && rgba[7] == 120 && rgba[11] == 0 && rgba[15] == 255);
  CHECK(rgba[16 + 3] == 40);
}

void TestBc3FlatColors() {
  const uint8_t colors[][4] = {{255, 255, 255, 255}, {0, 0, 0, 0},    {255, 0, 0, 128},  {8, 4, 8, 17},
                               {37, 201, 99, 77},    {1, 2, 3, 254}, {128, 128, 128, 1}, {250, 5, 131, 200}};
  bool representable_exact = true;
  int worst = 0;
  for (const auto& color : colors) {
    const Image image = Filled(8, 8, color);
    const Image decoded = Bc3RoundTrip(image);
    CHECK(MaxError(image, decoded, 3, 3) == 0);
    worst = std::max(worst, MaxError(image, decoded, 0, 2));
  }
  CHECK(worst <= 1);
  for (int r = 0; r < 32; ++r) {
    for (int g = 0; g < 64; g += 7) {
      const int b = 31 - r;
      const uint8_t color[4] = {uint8_t(r << 3 | r >> 2), uint8_t(g << 2 | g >> 4), uint8_t(b << 3 | b >> 2), 255};
      const Image image = Filled(4, 4, color);
      representable_exact &= MaxError(image, Bc3RoundTrip(image), 0, 3) == 0;
    }
  }
  CHECK(representable_exact);
}

void TestBc3Gradients() {
  Image image = MakeImage(64, 64);
  for (uint32_t y = 0; y < 64; ++y) {
    for (uint32_t x = 0; x < 64; ++x) {
      uint8_t* p = image.rgba.data() + (size_t(y) * 64 + x) * 4;
      p[0] = uint8_t(x * 4);
      p[1] = uint8_t(y * 4);
      p[2] = 128;
      p[3] = uint8_t(255 - x * 4);
    }
  }
  const Image decoded = Bc3RoundTrip(image);
  CHECK(MaxError(image, decoded, 0, 2) <= 10);
  CHECK(MaxError(image, decoded, 3, 3) <= 1);
  double squared = 0;
  for (size_t i = 0; i < image.rgba.size(); i += 4) {
    for (int c = 0; c < 3; ++c) {
      const double d = double(image.rgba[i + c]) - decoded.rgba[i + c];
      squared += d * d;
    }
  }
  CHECK(std::sqrt(squared / (64.0 * 64.0 * 3.0)) <= 4.0);

  // A smooth one-dimensional ramp lies on a line: nearly exact.
  Image ramp = MakeImage(64, 4);
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 64; ++x) {
      uint8_t* p = ramp.rgba.data() + (size_t(y) * 64 + x) * 4;
      p[0] = p[1] = p[2] = uint8_t(x * 4);
      p[3] = 255;
    }
  }
  CHECK(MaxError(ramp, Bc3RoundTrip(ramp), 0, 3) <= 4);
}

void TestBc3AlphaEdges() {
  // Hard edge: transparent black beside opaque white, both exact.
  Image edge = MakeImage(8, 8);
  for (uint32_t y = 0; y < 8; ++y) {
    for (uint32_t x = 4; x < 8; ++x) {
      std::fill_n(edge.rgba.data() + (size_t(y) * 8 + x) * 4, 4, uint8_t(255));
    }
  }
  const Image decoded_edge = Bc3RoundTrip(edge);
  CHECK(MaxError(edge, decoded_edge, 0, 3) == 0);

  // Antialiased text edge: 0 and 255 stay exact, the in-between steps stay close.
  const uint8_t steps[4] = {0, 17, 136, 255};
  Image text = MakeImage(4, 4);
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      uint8_t* p = text.rgba.data() + (size_t(y) * 4 + x) * 4;
      p[0] = p[1] = p[2] = 255;
      p[3] = steps[(x + y) % 4];
    }
  }
  const Image decoded_text = Bc3RoundTrip(text);
  CHECK(MaxError(text, decoded_text, 3, 3) <= 2);
  CHECK(MaxError(text, decoded_text, 0, 2) == 0);
  bool extremes_exact = true;
  for (size_t i = 3; i < text.rgba.size(); i += 4) {
    if (text.rgba[i] == 0 || text.rgba[i] == 255) {
      extremes_exact &= decoded_text.rgba[i] == text.rgba[i];
    }
  }
  CHECK(extremes_exact);

  // Odd sizes encode with padded edge blocks.
  const uint8_t gray[4] = {90, 90, 90, 200};
  const Image odd = Filled(5, 3, gray);
  CHECK(EncodeBc3(odd).size() == 2 * 1 * 16);
  CHECK(MaxError(odd, Bc3RoundTrip(odd), 0, 3) <= 1);
}

void TestDdsMips() {
  const uint8_t teal[4] = {0, 128, 128, 255};
  const Image image = Filled(64, 32, teal);
  const std::vector<uint8_t> dds = WriteDdsBc3(image, true);
  CHECK(GetLe32(dds, 0) == 0x20534444);
  CHECK(GetLe32(dds, 4) == 124);
  CHECK(GetLe32(dds, 8) == (0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000));
  CHECK(GetLe32(dds, 12) == 32 && GetLe32(dds, 16) == 64);
  CHECK(GetLe32(dds, 20) == 16 * 8 * 16);
  CHECK(GetLe32(dds, 28) == 7);  // 64x32, 32x16, 16x8, 8x4, 4x2, 2x1, 1x1
  CHECK(GetLe32(dds, 76) == 32 && GetLe32(dds, 80) == 0x4 && GetLe32(dds, 84) == 0x35545844);
  CHECK(GetLe32(dds, 108) == (0x1000 | 0x8 | 0x400000));
  CHECK(dds.size() == 128 + 16 * (16 * 8 + 8 * 4 + 4 * 2 + 2 * 1 + 1 + 1 + 1));
  const std::optional<Image> read = ReadDds(dds);
  CHECK(read && read->width == 64 && read->height == 32 && MaxError(*read, image, 0, 3) <= 1);
  // The last level is the 1x1 average.
  CHECK(std::equal(dds.end() - 16, dds.end(), dds.end() - 32));

  const std::vector<uint8_t> single = WriteDdsBc3(image, false);
  CHECK(single.size() == 128 + 16 * 16 * 8);
  CHECK(GetLe32(single, 8) == (0x1 | 0x2 | 0x4 | 0x1000 | 0x80000));
  CHECK(GetLe32(single, 28) == 0 && GetLe32(single, 108) == 0x1000);
  CHECK(WriteDdsBc3(Image{}, true).empty());
}

std::vector<uint8_t> DdsHeader(uint32_t width, uint32_t height, uint32_t pf_flags, uint32_t four_cc, uint32_t bits,
                               const uint32_t (&masks)[4]) {
  std::vector<uint8_t> out(128, 0);
  PutLe32(out, 0, 0x20534444);
  PutLe32(out, 4, 124);
  PutLe32(out, 8, 0x1007);
  PutLe32(out, 12, height);
  PutLe32(out, 16, width);
  PutLe32(out, 76, 32);
  PutLe32(out, 80, pf_flags);
  PutLe32(out, 84, four_cc);
  PutLe32(out, 88, bits);
  for (int i = 0; i < 4; ++i) {
    PutLe32(out, 92 + 4 * i, masks[i]);
  }
  PutLe32(out, 108, 0x1000);
  return out;
}

void TestReadDds() {
  // A8R8G8B8 (BGRA in memory), straight alpha.
  std::vector<uint8_t> argb = DdsHeader(2, 1, 0x41, 0, 32, {0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000});
  for (uint8_t b : {0x30, 0x20, 0x10, 0x80, 0xCC, 0xBB, 0xAA, 0x00}) {
    argb.push_back(b);
  }
  const std::optional<Image> image = ReadDds(argb);
  CHECK(image && image->width == 2 && image->height == 1);
  if (image) {
    const uint8_t first[4] = {0x10, 0x20, 0x30, 0x80};
    const uint8_t second[4] = {0xAA, 0xBB, 0xCC, 0x00};
    CHECK(PixelIs(*image, 0, 0, first) && PixelIs(*image, 1, 0, second));
  }
  // Without DDPF_ALPHAPIXELS the alpha mask is ignored.
  std::vector<uint8_t> xrgb = argb;
  PutLe32(xrgb, 80, 0x40);
  const std::optional<Image> opaque = ReadDds(xrgb);
  CHECK(opaque && Pixel(*opaque, 1, 0)[3] == 255);
  CHECK(!ReadDds(std::span(argb.data(), argb.size() - 1)));

  // DXT3, 4x4: one block.
  std::vector<uint8_t> dxt3 = DdsHeader(4, 4, 0x4, 0x33545844, 0, {0, 0, 0, 0});
  const uint8_t block[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
  dxt3.insert(dxt3.end(), block, block + 16);
  const std::optional<Image> red = ReadDds(dxt3);
  const uint8_t opaque_red[4] = {255, 0, 0, 255};
  CHECK(red && PixelIs(*red, 3, 3, opaque_red));
  CHECK(!ReadDds(std::span(dxt3.data(), dxt3.size() - 1)));

  // DX10 header naming BC3.
  std::vector<uint8_t> dx10 = DdsHeader(4, 4, 0x4, 0x30315844, 0, {0, 0, 0, 0});
  dx10.resize(148, 0);
  PutLe32(dx10, 128, 77);
  dx10.insert(dx10.end(), block, block + 16);
  CHECK(ReadDds(dx10).has_value());

  std::vector<uint8_t> bad_magic = dxt3;
  bad_magic[0] = 'X';
  CHECK(!ReadDds(bad_magic));
  std::vector<uint8_t> huge = dxt3;
  PutLe32(huge, 16, 1u << 20);
  CHECK(!ReadDds(huge));
  std::vector<uint8_t> unknown = dxt3;
  PutLe32(unknown, 84, 0x31495441);  // ATI1
  CHECK(!ReadDds(unknown));
  CHECK(!ReadDds(std::span(dxt3.data(), 100)));
}

void TestDownsample() {
  Image image = MakeImage(2, 2);
  image.rgba[0] = 255;
  image.rgba[3] = 255;
  const Image half = Downsample2x(image);
  const uint8_t expected[4] = {255, 0, 0, 64};
  CHECK(half.width == 1 && half.height == 1 && PixelIs(half, 0, 0, expected));

  // Nothing opaque: the plain average, not black.
  const uint8_t clear_white[4] = {255, 255, 255, 0};
  const Image white = Downsample2x(Filled(4, 2, clear_white));
  CHECK(white.width == 2 && white.height == 1 && PixelIs(white, 1, 0, clear_white));
}

void TestUpscale() {
  const uint8_t white[4] = {255, 255, 255, 255};
  const uint8_t clear[4] = {0, 0, 0, 0};
  Image image = MakeImage(16, 16);
  for (uint32_t y = 0; y < 16; ++y) {
    for (uint32_t x = 0; x < 8; ++x) {
      std::memcpy(image.rgba.data() + (size_t(y) * 16 + x) * 4, white, 4);
    }
  }
  const Image big = Upscale(image, 2);
  CHECK(big.width == 32 && big.height == 32);
  bool flat = true;
  bool edge_stays_white = true;
  for (uint32_t y = 0; y < 32; ++y) {
    for (uint32_t x = 0; x < 32; ++x) {
      if (x < 13) {
        flat &= PixelIs(big, x, y, white);
      } else if (x > 18) {
        flat &= PixelIs(big, x, y, clear);
      } else if (Pixel(big, x, y)[3] >= 8) {
        // Alpha-weighted: the edge does not pick up the black of the transparent side.
        edge_stays_white &= Pixel(big, x, y)[0] >= 254;
      }
    }
  }
  CHECK(flat);
  CHECK(edge_stays_white);

  const uint8_t tint[4] = {100, 150, 200, 128};
  const Image tinted = Upscale(Filled(5, 3, tint), 3);
  CHECK(tinted.width == 15 && tinted.height == 9 && MaxError(tinted, Filled(15, 9, tint), 0, 3) == 0);
  CHECK(Upscale(image, 0).rgba.empty());
  CHECK(Upscale(image, 2000).rgba.empty());
}

void TestPasteFit() {
  const uint8_t green[4] = {0, 255, 0, 255};
  const uint8_t red[4] = {255, 0, 0, 255};
  const uint8_t clear[4] = {0, 0, 0, 0};

  // 20x10 into 16x16: 16x8, centered vertically.
  Image dst = Filled(40, 40, green);
  CHECK(PasteFit(dst, {10, 10, 26, 26}, Filled(20, 10, red), {0, 0, 20, 10}));
  bool layout = true;
  for (uint32_t y = 0; y < 40; ++y) {
    for (uint32_t x = 0; x < 40; ++x) {
      const bool inside = x >= 10 && x < 26 && y >= 10 && y < 26;
      const bool drawn = inside && y >= 14 && y < 22;
      layout &= PixelIs(dst, x, y, !inside ? green : drawn ? red : clear);
    }
  }
  CHECK(layout);

  // A tall source in a wide rect: centered horizontally; taps outside src_rect are not used.
  const uint8_t blue[4] = {0, 0, 255, 255};
  Image src = Filled(30, 30, blue);
  for (uint32_t y = 5; y < 25; ++y) {
    for (uint32_t x = 5; x < 15; ++x) {
      std::memcpy(src.rgba.data() + (size_t(y) * 30 + x) * 4, red, 4);
    }
  }
  Image wide = Filled(30, 12, green);
  CHECK(PasteFit(wide, {0, 1, 30, 11}, src, {5, 5, 15, 25}));
  bool centered = true;
  for (uint32_t y = 1; y < 11; ++y) {
    for (uint32_t x = 0; x < 30; ++x) {
      centered &= PixelIs(wide, x, y, x >= 12 && x < 17 ? red : clear);
    }
  }
  CHECK(centered);
  CHECK(PixelIs(wide, 0, 0, green) && PixelIs(wide, 29, 11, green));

  // Upscaling into a larger rect keeps the content and aspect.
  Image large = Filled(64, 64, green);
  CHECK(PasteFit(large, {0, 0, 64, 64}, src, {5, 5, 15, 25}));
  CHECK(PixelIs(large, 16, 32, red) && PixelIs(large, 47, 32, red) && PixelIs(large, 15, 32, clear) &&
        PixelIs(large, 48, 32, clear));

  const Image before = dst;
  CHECK(!PasteFit(dst, {30, 30, 41, 35}, src, {0, 0, 4, 4}));
  CHECK(!PasteFit(dst, {5, 5, 5, 9}, src, {0, 0, 4, 4}));
  CHECK(!PasteFit(dst, {0, 0, 4, 4}, src, {0, 0, 31, 4}));
  CHECK(dst.rgba == before.rgba);
}

}  // namespace

int main() {
  TestAbcRoundTrip();
  TestScaleGlyphRects();
  TestAbcRejectsBadInput();
  TestDecodeBc1();
  TestDecodeBc2();
  TestDecodeBc3();
  TestBc3FlatColors();
  TestBc3Gradients();
  TestBc3AlphaEdges();
  TestDdsMips();
  TestReadDds();
  TestDownsample();
  TestUpscale();
  TestPasteFit();
  return check::Finish("font_atlas_tests");
}
