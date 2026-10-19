#pragma once

#include <cstdint>
#include <initializer_list>

namespace crackdown {

// sub_82252150 (AudioCore) reads a 360x256-bit grid at 0x82D33F98. The title itself reads slightly past a row
// (column 256 is a normal last column), so spans near the table are kept as they are. A NaN angle converts to
// 0x80000000, which gives row -128 and a read 256 MB below the table.
inline constexpr int32_t kVisibilityGridRows = 360;
inline constexpr int32_t kVisibilityGridColumns = 256;
inline constexpr int32_t kVisibilityGridRowBytes = kVisibilityGridColumns / 8;
inline constexpr int64_t kVisibilityGridBytes = int64_t{kVisibilityGridRows} * kVisibilityGridRowBytes;
inline constexpr int64_t kVisibilityGridSlackBytes = 0x10000;

struct VisibilityGridSpan {
  int32_t first_row;
  int32_t last_row;
  int32_t first_column;
  int32_t last_column;
};

constexpr int64_t VisibilityGridByteOffset(int32_t row, int32_t column) {
  return int64_t{row} * kVisibilityGridRowBytes + (column >> 3);
}

constexpr bool VisibilityGridSpanNearTable(const VisibilityGridSpan& span) {
  for (const int32_t row : {span.first_row, span.last_row}) {
    for (const int32_t column : {span.first_column, span.last_column}) {
      const int64_t offset = VisibilityGridByteOffset(row, column);
      if (offset < -kVisibilityGridSlackBytes || offset >= kVisibilityGridBytes + kVisibilityGridSlackBytes) {
        return false;
      }
    }
  }
  return true;
}

constexpr VisibilityGridSpan ClampVisibilityGridSpan(VisibilityGridSpan span) {
  auto row = [](int32_t v) { return v >= 0 && v < kVisibilityGridRows ? v : 0; };
  auto column = [](int32_t v) { return v < 0 ? 0 : v >= kVisibilityGridColumns ? kVisibilityGridColumns - 1 : v; };
  return {row(span.first_row), row(span.last_row), column(span.first_column), column(span.last_column)};
}

}  // namespace crackdown
