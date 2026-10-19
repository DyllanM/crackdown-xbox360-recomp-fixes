#include "font_atlas.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace crackdown::font_atlas {
namespace {

uint16_t LoadBe16(const uint8_t* p) {
  return uint16_t(p[0] << 8 | p[1]);
}

uint32_t LoadBe32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint16_t LoadLe16(const uint8_t* p) {
  return uint16_t(p[0] | p[1] << 8);
}

uint32_t LoadLe32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

void AppendBe16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(uint8_t(value >> 8));
  out.push_back(uint8_t(value));
}

void AppendBe32(std::vector<uint8_t>& out, uint32_t value) {
  AppendBe16(out, uint16_t(value >> 16));
  AppendBe16(out, uint16_t(value));
}

void StoreLe16(uint8_t* p, uint16_t value) {
  p[0] = uint8_t(value);
  p[1] = uint8_t(value >> 8);
}

void StoreLe32(uint8_t* p, uint32_t value) {
  StoreLe16(p, uint16_t(value));
  StoreLe16(p + 2, uint16_t(value >> 16));
}

constexpr uint32_t FourCc(char a, char b, char c, char d) {
  return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}

// ---- BC blocks ----

int Expand5(int value) {
  return value << 3 | value >> 2;
}

int Expand6(int value) {
  return value << 2 | value >> 4;
}

void Unpack565(uint16_t color, int rgb[3]) {
  rgb[0] = Expand5(color >> 11 & 31);
  rgb[1] = Expand6(color >> 5 & 63);
  rgb[2] = Expand5(color & 31);
}

// Palette entries 2 and 3 of four-color mode; the encoder and decoder share these.
int Third(int near, int far) {
  return (2 * near + far + 1) / 3;
}

void ColorPalette(uint16_t c0, uint16_t c1, bool four_color, int palette[4][4]) {
  Unpack565(c0, palette[0]);
  Unpack565(c1, palette[1]);
  palette[0][3] = palette[1][3] = 255;
  for (int c = 0; c < 3; ++c) {
    if (four_color) {
      palette[2][c] = Third(palette[0][c], palette[1][c]);
      palette[3][c] = Third(palette[1][c], palette[0][c]);
    } else {
      palette[2][c] = (palette[0][c] + palette[1][c] + 1) / 2;
      palette[3][c] = 0;
    }
  }
  palette[2][3] = 255;
  palette[3][3] = four_color ? 255 : 0;
}

void DecodeColor(const uint8_t* block, bool bc1, uint8_t rgba[64]) {
  const uint16_t c0 = LoadLe16(block);
  const uint16_t c1 = LoadLe16(block + 2);
  const uint32_t indices = LoadLe32(block + 4);
  int palette[4][4];
  ColorPalette(c0, c1, !bc1 || c0 > c1, palette);
  for (int i = 0; i < 16; ++i) {
    const int* entry = palette[indices >> (2 * i) & 3];
    for (int c = 0; c < 4; ++c) {
      rgba[i * 4 + c] = uint8_t(entry[c]);
    }
  }
}

void AlphaPalette(int a0, int a1, int palette[8]) {
  palette[0] = a0;
  palette[1] = a1;
  if (a0 > a1) {
    for (int i = 2; i < 8; ++i) {
      palette[i] = ((8 - i) * a0 + (i - 1) * a1 + 3) / 7;
    }
  } else {
    for (int i = 2; i < 6; ++i) {
      palette[i] = ((6 - i) * a0 + (i - 1) * a1 + 2) / 5;
    }
    palette[6] = 0;
    palette[7] = 255;
  }
}

void DecodeAlpha(const uint8_t* block, uint8_t rgba[64]) {
  int palette[8];
  AlphaPalette(block[0], block[1], palette);
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) {
    bits |= uint64_t(block[2 + i]) << (8 * i);
  }
  for (int i = 0; i < 16; ++i) {
    rgba[i * 4 + 3] = uint8_t(palette[bits >> (3 * i) & 7]);
  }
}

struct SingleColorTables {
  uint8_t five[256][2];
  uint8_t six[256][2];
};

// Endpoint pairs whose 2/3 entry is closest to each 8-bit value, so flat blocks lose almost nothing to 565.
const SingleColorTables& SingleColor() {
  static const SingleColorTables tables = [] {
    SingleColorTables t{};
    auto fill = [](uint8_t (*table)[2], int bits) {
      const int top = (1 << bits) - 1;
      for (int value = 0; value < 256; ++value) {
        int best = 1 << 30;
        for (int a = 0; a <= top; ++a) {
          for (int b = 0; b <= top; ++b) {
            const int ea = bits == 5 ? Expand5(a) : Expand6(a);
            const int eb = bits == 5 ? Expand5(b) : Expand6(b);
            const int score = std::abs(Third(ea, eb) - value) * 1024 + std::abs(ea - eb);
            if (score < best) {
              best = score;
              table[value][0] = uint8_t(a);
              table[value][1] = uint8_t(b);
            }
          }
        }
      }
    };
    fill(t.five, 5);
    fill(t.six, 6);
    return t;
  }();
  return tables;
}

uint16_t Pack565(int r, int g, int b) {
  return uint16_t(r << 11 | g << 5 | b);
}

uint16_t Quantize565(const float rgb[3]) {
  auto q = [](float value, int top) { return int(std::lround(std::clamp(value, 0.0f, 255.0f) * top / 255.0f)); };
  return Pack565(q(rgb[0], 31), q(rgb[1], 63), q(rgb[2], 31));
}

struct ColorFit {
  uint16_t c0 = 0;
  uint16_t c1 = 0;
  uint32_t indices = 0;
  float error = 0.0f;
};

// Puts c0 > c1 (four-color mode in every decoder) and picks each pixel's nearest entry.
ColorFit FitColor(uint16_t c0, uint16_t c1, const float pixels[16][3], const float weights[16]) {
  ColorFit fit;
  if (c0 < c1) {
    std::swap(c0, c1);
  }
  fit.c0 = c0;
  fit.c1 = c1;
  int palette[4][4];
  ColorPalette(c0, c1, true, palette);
  const int entries = c0 == c1 ? 1 : 4;
  for (int i = 0; i < 16; ++i) {
    float best = 1e30f;
    int best_index = 0;
    for (int e = 0; e < entries; ++e) {
      const float dr = pixels[i][0] - float(palette[e][0]);
      const float dg = pixels[i][1] - float(palette[e][1]);
      const float db = pixels[i][2] - float(palette[e][2]);
      const float d = dr * dr + dg * dg + db * db;
      if (d < best) {
        best = d;
        best_index = e;
      }
    }
    fit.indices |= uint32_t(best_index) << (2 * i);
    fit.error += weights[i] * best;
  }
  return fit;
}

// Least-squares endpoints for the current index assignment.
bool RefineEndpoints(const ColorFit& fit, const float pixels[16][3], const float weights[16], float e0[3],
                     float e1[3]) {
  static constexpr float kT[4] = {0.0f, 1.0f, 1.0f / 3.0f, 2.0f / 3.0f};
  float aa = 0, bb = 0, ab = 0;
  float ax[3] = {}, bx[3] = {};
  for (int i = 0; i < 16; ++i) {
    const float t = kT[fit.indices >> (2 * i) & 3];
    const float s = 1.0f - t;
    const float w = weights[i];
    aa += w * s * s;
    bb += w * t * t;
    ab += w * s * t;
    for (int c = 0; c < 3; ++c) {
      ax[c] += w * s * pixels[i][c];
      bx[c] += w * t * pixels[i][c];
    }
  }
  const float det = aa * bb - ab * ab;
  if (std::abs(det) < 1e-6f * std::max(aa * bb, 1e-12f)) {
    return false;
  }
  for (int c = 0; c < 3; ++c) {
    e0[c] = (ax[c] * bb - bx[c] * ab) / det;
    e1[c] = (bx[c] * aa - ax[c] * ab) / det;
  }
  return true;
}

void EncodeColor(const uint8_t rgba[64], uint8_t out[8]) {
  bool uniform = true;
  for (int i = 1; i < 16 && uniform; ++i) {
    uniform = std::memcmp(rgba + i * 4, rgba, 3) == 0;
  }
  ColorFit fit;
  if (uniform) {
    const SingleColorTables& t = SingleColor();
    const uint16_t c0 = Pack565(t.five[rgba[0]][0], t.six[rgba[1]][0], t.five[rgba[2]][0]);
    const uint16_t c1 = Pack565(t.five[rgba[0]][1], t.six[rgba[1]][1], t.five[rgba[2]][1]);
    // Every pixel takes the entry two thirds of the way from c0 to c1.
    fit.c0 = std::max(c0, c1);
    fit.c1 = std::min(c0, c1);
    const uint32_t index = c0 == c1 ? 0 : c0 > c1 ? 2 : 3;
    for (int i = 0; i < 16; ++i) {
      fit.indices |= index << (2 * i);
    }
  } else {
    // Transparent texels still count a little: bilinear filtering blends their color into the edges.
    float pixels[16][3];
    float weights[16];
    float mean[3] = {};
    float total = 0.0f;
    for (int i = 0; i < 16; ++i) {
      weights[i] = 1.0f + rgba[i * 4 + 3];
      total += weights[i];
      for (int c = 0; c < 3; ++c) {
        pixels[i][c] = rgba[i * 4 + c];
        mean[c] += weights[i] * pixels[i][c];
      }
    }
    for (float& m : mean) {
      m /= total;
    }
    float cov[3][3] = {};
    for (int i = 0; i < 16; ++i) {
      const float d[3] = {pixels[i][0] - mean[0], pixels[i][1] - mean[1], pixels[i][2] - mean[2]};
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
          cov[r][c] += weights[i] * d[r] * d[c];
        }
      }
    }
    // Principal axis by power iteration, starting from the row of the widest channel.
    int widest = 0;
    for (int c = 1; c < 3; ++c) {
      widest = cov[c][c] > cov[widest][widest] ? c : widest;
    }
    float axis[3] = {cov[widest][0], cov[widest][1], cov[widest][2]};
    for (int iteration = 0; iteration < 8; ++iteration) {
      float next[3];
      for (int r = 0; r < 3; ++r) {
        next[r] = cov[r][0] * axis[0] + cov[r][1] * axis[1] + cov[r][2] * axis[2];
      }
      const float scale = std::max({std::abs(next[0]), std::abs(next[1]), std::abs(next[2])});
      if (scale <= 0.0f) {
        break;
      }
      for (int c = 0; c < 3; ++c) {
        axis[c] = next[c] / scale;
      }
    }
    float length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (length <= 1e-6f) {
      axis[0] = axis[1] = axis[2] = 1.0f;
      length = std::sqrt(3.0f);
    }
    for (float& a : axis) {
      a /= length;
    }
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < 16; ++i) {
      const float t =
          (pixels[i][0] - mean[0]) * axis[0] + (pixels[i][1] - mean[1]) * axis[1] + (pixels[i][2] - mean[2]) * axis[2];
      lo = std::min(lo, t);
      hi = std::max(hi, t);
    }
    // Extremes as they are, and inset by 1/16 of the range (better for gradients); keep the closer.
    fit.error = 1e30f;
    for (const float inset : {0.0f, (hi - lo) / 16.0f}) {
      float e0[3], e1[3];
      for (int c = 0; c < 3; ++c) {
        e0[c] = mean[c] + (hi - inset) * axis[c];
        e1[c] = mean[c] + (lo + inset) * axis[c];
      }
      const ColorFit candidate = FitColor(Quantize565(e0), Quantize565(e1), pixels, weights);
      if (candidate.error < fit.error) {
        fit = candidate;
      }
    }
    for (int iteration = 0; iteration < 2 && fit.error > 0.0f; ++iteration) {
      float e0[3], e1[3];
      if (!RefineEndpoints(fit, pixels, weights, e0, e1)) {
        break;
      }
      const ColorFit candidate = FitColor(Quantize565(e0), Quantize565(e1), pixels, weights);
      if (candidate.error >= fit.error) {
        break;
      }
      fit = candidate;
    }
  }
  StoreLe16(out, fit.c0);
  StoreLe16(out + 2, fit.c1);
  StoreLe32(out + 4, fit.indices);
}

struct AlphaFit {
  uint64_t indices = 0;
  int error = 0;
};

AlphaFit FitAlpha(int a0, int a1, const int alpha[16]) {
  int palette[8];
  AlphaPalette(a0, a1, palette);
  AlphaFit fit;
  for (int i = 0; i < 16; ++i) {
    int best = 1 << 30;
    int best_index = 0;
    for (int e = 0; e < 8; ++e) {
      const int d = std::abs(alpha[i] - palette[e]);
      if (d < best) {
        best = d;
        best_index = e;
      }
    }
    fit.indices |= uint64_t(best_index) << (3 * i);
    fit.error += best * best;
  }
  return fit;
}

void EncodeAlpha(const uint8_t rgba[64], uint8_t out[8]) {
  int alpha[16];
  int lo = 255, hi = 0, inner_lo = 255, inner_hi = 0;
  for (int i = 0; i < 16; ++i) {
    const int a = alpha[i] = rgba[i * 4 + 3];
    lo = std::min(lo, a);
    hi = std::max(hi, a);
    if (a != 0 && a != 255) {
      inner_lo = std::min(inner_lo, a);
      inner_hi = std::max(inner_hi, a);
    }
  }
  int a0 = lo, a1 = lo;
  AlphaFit fit;
  if (lo != hi) {
    // Eight interpolated values between the extremes, or six between the inner values plus exact 0 and 255.
    a0 = hi;
    a1 = lo;
    fit = FitAlpha(a0, a1, alpha);
    if (inner_lo > inner_hi) {
      inner_lo = inner_hi = 0;
    }
    const AlphaFit six = FitAlpha(inner_lo, inner_hi, alpha);
    if (six.error < fit.error) {
      a0 = inner_lo;
      a1 = inner_hi;
      fit = six;
    }
  }
  out[0] = uint8_t(a0);
  out[1] = uint8_t(a1);
  for (int i = 0; i < 6; ++i) {
    out[2 + i] = uint8_t(fit.indices >> (8 * i));
  }
}

size_t BlockBytes(BlockFormat format) {
  return format == BlockFormat::kBc1 ? 8 : 16;
}

// ---- DDS ----

constexpr uint32_t kDdsMagic = FourCc('D', 'D', 'S', ' ');
constexpr size_t kDdsHeaderBytes = 128;
constexpr size_t kDx10HeaderBytes = 20;
constexpr uint32_t kMaxDdsSide = 16384;

constexpr uint32_t kDdsdCaps = 0x1;
constexpr uint32_t kDdsdHeight = 0x2;
constexpr uint32_t kDdsdWidth = 0x4;
constexpr uint32_t kDdsdPixelFormat = 0x1000;
constexpr uint32_t kDdsdMipMapCount = 0x20000;
constexpr uint32_t kDdsdLinearSize = 0x80000;
constexpr uint32_t kDdpfAlphaPixels = 0x1;
constexpr uint32_t kDdpfFourCc = 0x4;
constexpr uint32_t kDdpfRgb = 0x40;
constexpr uint32_t kDdsCapsComplex = 0x8;
constexpr uint32_t kDdsCapsTexture = 0x1000;
constexpr uint32_t kDdsCapsMipMap = 0x400000;

struct ChannelMask {
  uint32_t mask = 0;
  int shift = 0;
  uint32_t top = 0;
};

// Contiguous masks of 1 to 8 bits; an empty mask reads as 255.
std::optional<ChannelMask> MakeChannelMask(uint32_t mask) {
  ChannelMask channel{mask, 0, 0};
  if (mask == 0) {
    return channel;
  }
  channel.shift = std::countr_zero(mask);
  channel.top = mask >> channel.shift;
  if (std::popcount(channel.top) > 8 || (channel.top & (channel.top + 1)) != 0) {
    return std::nullopt;
  }
  return channel;
}

uint8_t ReadChannel(uint32_t pixel, const ChannelMask& channel) {
  if (channel.mask == 0) {
    return 255;
  }
  const uint32_t value = (pixel & channel.mask) >> channel.shift;
  return uint8_t((value * 255 + channel.top / 2) / channel.top);
}

std::optional<Image> DecodeMasked(uint32_t width, uint32_t height, std::span<const uint8_t> data,
                                  const uint32_t masks[4]) {
  ChannelMask channels[4];
  for (int c = 0; c < 4; ++c) {
    const std::optional<ChannelMask> channel = MakeChannelMask(masks[c]);
    if (!channel || (c < 3 && masks[c] == 0)) {
      return std::nullopt;
    }
    channels[c] = *channel;
  }
  const size_t pixels = size_t(width) * height;
  if (data.size() < pixels * 4) {
    return std::nullopt;
  }
  Image image = MakeImage(width, height);
  for (size_t i = 0; i < pixels; ++i) {
    const uint32_t pixel = LoadLe32(data.data() + i * 4);
    for (int c = 0; c < 4; ++c) {
      image.rgba[i * 4 + c] = ReadChannel(pixel, channels[c]);
    }
  }
  return image;
}

std::optional<Image> ReadDx10(uint32_t width, uint32_t height, std::span<const uint8_t> bytes) {
  if (bytes.size() < kDdsHeaderBytes + kDx10HeaderBytes) {
    return std::nullopt;
  }
  const std::span<const uint8_t> data = bytes.subspan(kDdsHeaderBytes + kDx10HeaderBytes);
  static constexpr uint32_t kRgba[4] = {0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000};
  static constexpr uint32_t kBgra[4] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000};
  static constexpr uint32_t kBgrx[4] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0};
  switch (LoadLe32(bytes.data() + kDdsHeaderBytes)) {
    case 71:  // BC1_UNORM
    case 72:
      return DecodeBlocks(BlockFormat::kBc1, width, height, data);
    case 74:  // BC2_UNORM
    case 75:
      return DecodeBlocks(BlockFormat::kBc2, width, height, data);
    case 77:  // BC3_UNORM
    case 78:
      return DecodeBlocks(BlockFormat::kBc3, width, height, data);
    case 28:  // R8G8B8A8_UNORM
    case 29:
      return DecodeMasked(width, height, data, kRgba);
    case 87:  // B8G8R8A8_UNORM
    case 91:
      return DecodeMasked(width, height, data, kBgra);
    case 88:  // B8G8R8X8_UNORM
    case 93:
      return DecodeMasked(width, height, data, kBgrx);
    default:
      return std::nullopt;
  }
}

// ---- Resampling ----

float CatmullRom(float x) {
  x = std::abs(x);
  if (x < 1.0f) {
    return (1.5f * x - 2.5f) * x * x + 1.0f;
  }
  if (x < 2.0f) {
    return ((-0.5f * x + 2.5f) * x - 4.0f) * x + 2.0f;
  }
  return 0.0f;
}

// Per output coordinate: the first source index, the tap count and `taps` weights (sum 1).
struct AxisFilter {
  uint32_t taps = 0;
  std::vector<uint32_t> first;
  std::vector<uint32_t> count;
  std::vector<float> weights;
};

AxisFilter MakeAxisFilter(uint32_t in_lo, uint32_t in_len, uint32_t out_len) {
  const double scale = double(in_len) / out_len;
  const double filter_scale = std::max(scale, 1.0);
  const double support = 2.0 * filter_scale;
  AxisFilter filter;
  filter.taps = uint32_t(std::ceil(2.0 * support)) + 2;
  filter.first.resize(out_len);
  filter.count.resize(out_len);
  filter.weights.assign(size_t(out_len) * filter.taps, 0.0f);
  const int64_t in_hi = int64_t(in_lo) + in_len;
  for (uint32_t i = 0; i < out_len; ++i) {
    const double center = in_lo + (i + 0.5) * scale;
    const int64_t lo = std::max<int64_t>(in_lo, int64_t(std::floor(center - support)));
    const int64_t hi = std::min<int64_t>(in_hi, int64_t(std::ceil(center + support)));
    float* weights = filter.weights.data() + size_t(i) * filter.taps;
    double sum = 0.0;
    for (int64_t j = lo; j < hi; ++j) {
      const float w = CatmullRom(float((j + 0.5 - center) / filter_scale));
      weights[j - lo] = w;
      sum += w;
    }
    filter.first[i] = uint32_t(lo);
    filter.count[i] = uint32_t(hi - lo);
    if (std::abs(sum) < 1e-6) {
      filter.first[i] = uint32_t(std::clamp<int64_t>(int64_t(center), in_lo, in_hi - 1));
      filter.count[i] = 1;
      weights[0] = 1.0f;
      continue;
    }
    for (uint32_t k = 0; k < filter.count[i]; ++k) {
      weights[k] = float(weights[k] / sum);
    }
  }
  return filter;
}

// Accumulated channels: alpha, premultiplied rgb, plain rgb (the color used where nothing is opaque).
constexpr int kChannels = 7;

void StorePixel(const float* acc, uint8_t* out) {
  // A small bias toward the plain color keeps nearly transparent results from amplifying noise.
  constexpr float kBias = 1.0f / 4096.0f;
  const float alpha = acc[0];
  for (int c = 0; c < 3; ++c) {
    const float color = alpha > 0.0f ? (acc[1 + c] + kBias * acc[4 + c]) / (alpha + kBias) : acc[4 + c];
    out[c] = uint8_t(std::lround(std::clamp(color, 0.0f, 1.0f) * 255.0f));
  }
  out[3] = uint8_t(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
}

}  // namespace

Image MakeImage(uint32_t width, uint32_t height) {
  Image image;
  image.width = width;
  image.height = height;
  image.rgba.assign(size_t(width) * height * 4, 0);
  return image;
}

// ---- .abc ----

std::optional<FontMetrics> ParseAbc(std::span<const uint8_t> bytes) {
  constexpr size_t kMaxCharOffset = 0x14;
  constexpr size_t kMapOffset = 0x16;
  constexpr size_t kRecordBytes = 16;
  if (bytes.size() < kMapOffset || LoadBe32(bytes.data()) != 5) {
    return std::nullopt;
  }
  FontMetrics metrics;
  std::copy_n(bytes.begin(), metrics.header.size(), metrics.header.begin());
  metrics.max_char = LoadBe16(bytes.data() + kMaxCharOffset);
  const size_t map_count = size_t(metrics.max_char) + 1;
  const size_t count_offset = kMapOffset + 2 * map_count;
  if (bytes.size() < count_offset + 4) {
    return std::nullopt;
  }
  const uint32_t glyph_count = LoadBe32(bytes.data() + count_offset);
  const size_t records_offset = count_offset + 4;
  if (glyph_count == 0 || uint64_t(glyph_count) * kRecordBytes != bytes.size() - records_offset) {
    return std::nullopt;
  }
  metrics.map.resize(map_count);
  for (size_t c = 0; c < map_count; ++c) {
    metrics.map[c] = LoadBe16(bytes.data() + kMapOffset + 2 * c);
    if (metrics.map[c] >= glyph_count) {
      return std::nullopt;
    }
  }
  metrics.glyphs.resize(glyph_count);
  for (uint32_t i = 0; i < glyph_count; ++i) {
    const uint8_t* p = bytes.data() + records_offset + size_t(i) * kRecordBytes;
    GlyphRecord& glyph = metrics.glyphs[i];
    glyph.x0 = LoadBe16(p);
    glyph.y0 = LoadBe16(p + 2);
    glyph.x1 = LoadBe16(p + 4);
    glyph.y1 = LoadBe16(p + 6);
    glyph.xoff = int16_t(LoadBe16(p + 8));
    glyph.width = int16_t(LoadBe16(p + 10));
    glyph.advance = int16_t(LoadBe16(p + 12));
    glyph.channel_mask = LoadBe16(p + 14);
    if (glyph.x0 > glyph.x1 || glyph.y0 > glyph.y1) {
      return std::nullopt;
    }
  }
  return metrics;
}

std::vector<uint8_t> WriteAbc(const FontMetrics& metrics) {
  std::vector<uint8_t> out(metrics.header.begin(), metrics.header.end());
  out.reserve(out.size() + 6 + 2 * metrics.map.size() + 16 * metrics.glyphs.size());
  AppendBe16(out, metrics.max_char);
  for (uint16_t index : metrics.map) {
    AppendBe16(out, index);
  }
  AppendBe32(out, uint32_t(metrics.glyphs.size()));
  for (const GlyphRecord& glyph : metrics.glyphs) {
    for (uint16_t value : {glyph.x0, glyph.y0, glyph.x1, glyph.y1, uint16_t(glyph.xoff), uint16_t(glyph.width),
                           uint16_t(glyph.advance), glyph.channel_mask}) {
      AppendBe16(out, value);
    }
  }
  return out;
}

bool ScaleGlyphRects(FontMetrics& metrics, uint32_t factor) {
  if (factor == 0) {
    return false;
  }
  for (const GlyphRecord& glyph : metrics.glyphs) {
    if (uint64_t(std::max(glyph.x1, glyph.y1)) * factor > 0xFFFF) {
      return false;
    }
  }
  for (GlyphRecord& glyph : metrics.glyphs) {
    glyph.x0 = uint16_t(glyph.x0 * factor);
    glyph.y0 = uint16_t(glyph.y0 * factor);
    glyph.x1 = uint16_t(glyph.x1 * factor);
    glyph.y1 = uint16_t(glyph.y1 * factor);
  }
  return true;
}

const GlyphRecord* GlyphFor(const FontMetrics& metrics, char16_t c) {
  if (c >= metrics.map.size() || metrics.map[c] >= metrics.glyphs.size()) {
    return nullptr;
  }
  return &metrics.glyphs[metrics.map[c]];
}

bool GlyphRectsFit(const FontMetrics& metrics, uint32_t width, uint32_t height) {
  return std::all_of(metrics.glyphs.begin(), metrics.glyphs.end(), [&](const GlyphRecord& glyph) {
    return glyph.x0 <= glyph.x1 && glyph.y0 <= glyph.y1 && glyph.x1 <= width && glyph.y1 <= height;
  });
}

// ---- Blocks ----

void DecodeBlock(BlockFormat format, const uint8_t* block, uint8_t rgba[64]) {
  switch (format) {
    case BlockFormat::kBc1:
      DecodeColor(block, true, rgba);
      return;
    case BlockFormat::kBc2: {
      DecodeColor(block + 8, false, rgba);
      for (int i = 0; i < 16; ++i) {
        rgba[i * 4 + 3] = uint8_t((block[i / 2] >> (4 * (i & 1)) & 15) * 17);
      }
      return;
    }
    case BlockFormat::kBc3:
      DecodeColor(block + 8, false, rgba);
      DecodeAlpha(block, rgba);
      return;
  }
}

void EncodeBc3Block(const uint8_t rgba[64], uint8_t block[16]) {
  EncodeAlpha(rgba, block);
  EncodeColor(rgba, block + 8);
}

std::vector<uint8_t> EncodeBc3(const Image& image) {
  const uint32_t blocks_x = (image.width + 3) / 4;
  const uint32_t blocks_y = (image.height + 3) / 4;
  std::vector<uint8_t> out(size_t(blocks_x) * blocks_y * 16);
  uint8_t pixels[64];
  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      for (uint32_t y = 0; y < 4; ++y) {
        const uint32_t sy = std::min(by * 4 + y, image.height - 1);
        const uint8_t* row = image.rgba.data() + size_t(sy) * image.width * 4;
        for (uint32_t x = 0; x < 4; ++x) {
          const uint32_t sx = std::min(bx * 4 + x, image.width - 1);
          std::memcpy(pixels + (y * 4 + x) * 4, row + size_t(sx) * 4, 4);
        }
      }
      EncodeBc3Block(pixels, out.data() + (size_t(by) * blocks_x + bx) * 16);
    }
  }
  return out;
}

std::optional<Image> DecodeBlocks(BlockFormat format, uint32_t width, uint32_t height, std::span<const uint8_t> data) {
  const uint32_t blocks_x = (width + 3) / 4;
  const uint32_t blocks_y = (height + 3) / 4;
  const size_t block_bytes = BlockBytes(format);
  if (width == 0 || height == 0 || data.size() < size_t(blocks_x) * blocks_y * block_bytes) {
    return std::nullopt;
  }
  Image image = MakeImage(width, height);
  uint8_t pixels[64];
  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      DecodeBlock(format, data.data() + (size_t(by) * blocks_x + bx) * block_bytes, pixels);
      for (uint32_t y = 0; y < 4 && by * 4 + y < height; ++y) {
        const uint32_t columns = std::min<uint32_t>(4, width - bx * 4);
        std::memcpy(image.rgba.data() + (size_t(by * 4 + y) * width + bx * 4) * 4, pixels + y * 16, columns * 4);
      }
    }
  }
  return image;
}

// ---- DDS ----

std::optional<Image> ReadDds(std::span<const uint8_t> bytes) {
  if (bytes.size() < kDdsHeaderBytes || LoadLe32(bytes.data()) != kDdsMagic || LoadLe32(bytes.data() + 4) != 124) {
    return std::nullopt;
  }
  const uint32_t height = LoadLe32(bytes.data() + 12);
  const uint32_t width = LoadLe32(bytes.data() + 16);
  if (width == 0 || height == 0 || width > kMaxDdsSide || height > kMaxDdsSide) {
    return std::nullopt;
  }
  const uint32_t pf_flags = LoadLe32(bytes.data() + 80);
  const uint32_t four_cc = LoadLe32(bytes.data() + 84);
  const std::span<const uint8_t> data = bytes.subspan(kDdsHeaderBytes);
  if (pf_flags & kDdpfFourCc) {
    switch (four_cc) {
      case FourCc('D', 'X', 'T', '1'):
        return DecodeBlocks(BlockFormat::kBc1, width, height, data);
      case FourCc('D', 'X', 'T', '3'):
        return DecodeBlocks(BlockFormat::kBc2, width, height, data);
      case FourCc('D', 'X', 'T', '5'):
        return DecodeBlocks(BlockFormat::kBc3, width, height, data);
      case FourCc('D', 'X', '1', '0'):
        return ReadDx10(width, height, bytes);
      default:
        return std::nullopt;
    }
  }
  if ((pf_flags & kDdpfRgb) && LoadLe32(bytes.data() + 88) == 32) {
    const uint32_t masks[4] = {LoadLe32(bytes.data() + 92), LoadLe32(bytes.data() + 96), LoadLe32(bytes.data() + 100),
                               (pf_flags & kDdpfAlphaPixels) ? LoadLe32(bytes.data() + 104) : 0};
    return DecodeMasked(width, height, data, masks);
  }
  return std::nullopt;
}

std::vector<uint8_t> WriteDdsBc3(const Image& image, bool mips) {
  if (image.width == 0 || image.height == 0) {
    return {};
  }
  std::vector<std::vector<uint8_t>> levels;
  levels.push_back(EncodeBc3(image));
  if (mips) {
    Image level = Downsample2x(image);
    for (;;) {
      levels.push_back(EncodeBc3(level));
      if (level.width == 1 && level.height == 1) {
        break;
      }
      level = Downsample2x(level);
    }
  }
  size_t total = kDdsHeaderBytes;
  for (const std::vector<uint8_t>& level : levels) {
    total += level.size();
  }
  std::vector<uint8_t> out(kDdsHeaderBytes, 0);
  out.reserve(total);
  uint8_t* header = out.data();
  StoreLe32(header, kDdsMagic);
  StoreLe32(header + 4, 124);
  StoreLe32(header + 8, kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPixelFormat | kDdsdLinearSize |
                            (mips ? kDdsdMipMapCount : 0));
  StoreLe32(header + 12, image.height);
  StoreLe32(header + 16, image.width);
  StoreLe32(header + 20, uint32_t(levels[0].size()));
  StoreLe32(header + 28, mips ? uint32_t(levels.size()) : 0);
  StoreLe32(header + 76, 32);
  StoreLe32(header + 80, kDdpfFourCc);
  StoreLe32(header + 84, FourCc('D', 'X', 'T', '5'));
  StoreLe32(header + 108, kDdsCapsTexture | (mips ? kDdsCapsComplex | kDdsCapsMipMap : 0));
  for (const std::vector<uint8_t>& level : levels) {
    out.insert(out.end(), level.begin(), level.end());
  }
  return out;
}

// ---- Resampling ----

Image Downsample2x(const Image& image) {
  if (image.width == 0 || image.height == 0) {
    return {};
  }
  Image out = MakeImage(std::max(1u, image.width / 2), std::max(1u, image.height / 2));
  for (uint32_t y = 0; y < out.height; ++y) {
    const uint32_t rows[2] = {std::min(2 * y, image.height - 1), std::min(2 * y + 1, image.height - 1)};
    for (uint32_t x = 0; x < out.width; ++x) {
      const uint32_t columns[2] = {std::min(2 * x, image.width - 1), std::min(2 * x + 1, image.width - 1)};
      uint32_t alpha = 0;
      uint32_t premultiplied[3] = {};
      uint32_t plain[3] = {};
      for (uint32_t row : rows) {
        for (uint32_t column : columns) {
          const uint8_t* p = image.rgba.data() + (size_t(row) * image.width + column) * 4;
          alpha += p[3];
          for (int c = 0; c < 3; ++c) {
            premultiplied[c] += p[c] * p[3];
            plain[c] += p[c];
          }
        }
      }
      uint8_t* q = out.rgba.data() + (size_t(y) * out.width + x) * 4;
      for (int c = 0; c < 3; ++c) {
        q[c] = uint8_t(alpha ? (premultiplied[c] + alpha / 2) / alpha : (plain[c] + 2) / 4);
      }
      q[3] = uint8_t((alpha + 2) / 4);
    }
  }
  return out;
}

Image Resample(const Image& src, Rect src_rect, uint32_t out_width, uint32_t out_height) {
  if (out_width == 0 || out_height == 0 || src_rect.x0 >= src_rect.x1 || src_rect.y0 >= src_rect.y1 ||
      src_rect.x1 > src.width || src_rect.y1 > src.height || src.rgba.size() != size_t(src.width) * src.height * 4) {
    return {};
  }
  const AxisFilter fx = MakeAxisFilter(src_rect.x0, src_rect.width(), out_width);
  const AxisFilter fy = MakeAxisFilter(src_rect.y0, src_rect.height(), out_height);
  float unit[256];
  for (int i = 0; i < 256; ++i) {
    unit[i] = i / 255.0f;
  }
  // Horizontally filtered source rows, a ring indexed by source row; any output row needs fewer than `taps` rows.
  const size_t row_floats = size_t(out_width) * kChannels;
  std::vector<float> ring(size_t(fy.taps) * row_floats);
  std::vector<int64_t> ring_rows(fy.taps, -1);
  auto filtered_row = [&](uint32_t sy) -> const float* {
    float* row = ring.data() + (sy % fy.taps) * row_floats;
    if (ring_rows[sy % fy.taps] == sy) {
      return row;
    }
    ring_rows[sy % fy.taps] = sy;
    const uint8_t* line = src.rgba.data() + size_t(sy) * src.width * 4;
    for (uint32_t x = 0; x < out_width; ++x) {
      const float* weights = fx.weights.data() + size_t(x) * fx.taps;
      const uint8_t* p = line + size_t(fx.first[x]) * 4;
      float acc[kChannels] = {};
      for (uint32_t k = 0; k < fx.count[x]; ++k, p += 4) {
        const float w = weights[k];
        const float wa = w * unit[p[3]];
        acc[0] += wa;
        for (int c = 0; c < 3; ++c) {
          acc[1 + c] += wa * unit[p[c]];
          acc[4 + c] += w * unit[p[c]];
        }
      }
      std::copy_n(acc, kChannels, row + size_t(x) * kChannels);
    }
    return row;
  };
  Image out = MakeImage(out_width, out_height);
  std::vector<float> acc(row_floats);
  for (uint32_t y = 0; y < out_height; ++y) {
    std::fill(acc.begin(), acc.end(), 0.0f);
    const float* weights = fy.weights.data() + size_t(y) * fy.taps;
    for (uint32_t k = 0; k < fy.count[y]; ++k) {
      const float* row = filtered_row(fy.first[y] + k);
      const float w = weights[k];
      for (size_t i = 0; i < row_floats; ++i) {
        acc[i] += w * row[i];
      }
    }
    uint8_t* line = out.rgba.data() + size_t(y) * out_width * 4;
    for (uint32_t x = 0; x < out_width; ++x) {
      StorePixel(acc.data() + size_t(x) * kChannels, line + size_t(x) * 4);
    }
  }
  return out;
}

Image Upscale(const Image& image, uint32_t factor) {
  if (factor == 0 || uint64_t(image.width) * factor > kMaxDdsSide || uint64_t(image.height) * factor > kMaxDdsSide) {
    return {};
  }
  return Resample(image, {0, 0, image.width, image.height}, image.width * factor, image.height * factor);
}

bool PasteFit(Image& dst, Rect dst_rect, const Image& src, Rect src_rect) {
  if (dst_rect.x0 >= dst_rect.x1 || dst_rect.y0 >= dst_rect.y1 || dst_rect.x1 > dst.width ||
      dst_rect.y1 > dst.height || dst.rgba.size() != size_t(dst.width) * dst.height * 4) {
    return false;
  }
  const uint64_t sw = src_rect.width(), sh = src_rect.height();
  const uint64_t dw = dst_rect.width(), dh = dst_rect.height();
  uint32_t fit_w = uint32_t(dw), fit_h = uint32_t(dh);
  if (src_rect.x0 < src_rect.x1 && src_rect.y0 < src_rect.y1) {
    if (sw * dh <= sh * dw) {
      fit_w = uint32_t(std::clamp<uint64_t>((sw * dh + sh / 2) / sh, 1, dw));
    } else {
      fit_h = uint32_t(std::clamp<uint64_t>((sh * dw + sw / 2) / sw, 1, dh));
    }
  }
  const Image fitted = Resample(src, src_rect, fit_w, fit_h);
  if (fitted.rgba.empty()) {
    return false;
  }
  for (uint32_t y = dst_rect.y0; y < dst_rect.y1; ++y) {
    std::fill_n(dst.rgba.data() + (size_t(y) * dst.width + dst_rect.x0) * 4, dw * 4, uint8_t(0));
  }
  const uint32_t ox = dst_rect.x0 + uint32_t(dw - fit_w) / 2;
  const uint32_t oy = dst_rect.y0 + uint32_t(dh - fit_h) / 2;
  for (uint32_t y = 0; y < fit_h; ++y) {
    std::memcpy(dst.rgba.data() + (size_t(oy + y) * dst.width + ox) * 4, fitted.rgba.data() + size_t(y) * fit_w * 4,
                size_t(fit_w) * 4);
  }
  return true;
}

}  // namespace crackdown::font_atlas
