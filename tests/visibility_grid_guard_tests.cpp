#include <cstdint>

#include "check.h"
#include "visibility_grid_guard.h"

using crackdown::ClampVisibilityGridSpan;
using crackdown::VisibilityGridSpan;
using crackdown::VisibilityGridSpanNearTable;

namespace {

bool Same(const VisibilityGridSpan& a, const VisibilityGridSpan& b) {
  return a.first_row == b.first_row && a.last_row == b.last_row && a.first_column == b.first_column &&
         a.last_column == b.last_column;
}

}  // namespace

int main() {
  // Spans the title reads itself are kept, including a wrapping row range.
  CHECK(VisibilityGridSpanNearTable({0, 0, 0, 0}));
  CHECK(VisibilityGridSpanNearTable({359, 359, 255, 255}));
  CHECK(VisibilityGridSpanNearTable({350, 10, 7, 200}));
  // Column 256 is a normal last column (one bit of the next row's first byte).
  CHECK(VisibilityGridSpanNearTable({0, 167, 82, 256}));
  CHECK(VisibilityGridSpanNearTable({359, 359, 0, 256}));
  // Slightly outside the grid still reads the title's own data, as on the console.
  CHECK(VisibilityGridSpanNearTable({-1, 360, -8, 300}));

  // The crash: both angles NaN, fctiwz gives 0x80000000, the row is 0x80000000 % 360 = -128.
  const int32_t nan_int = static_cast<int32_t>(0x80000000u);
  const VisibilityGridSpan nan_span{nan_int % 360, nan_int % 360, nan_int, nan_int};
  CHECK(nan_span.first_row == -128);
  CHECK(!VisibilityGridSpanNearTable(nan_span));
  CHECK(Same(ClampVisibilityGridSpan(nan_span), {0, 0, 0, 0}));
  CHECK(VisibilityGridSpanNearTable(ClampVisibilityGridSpan(nan_span)));

  // Saturated positive conversions: row 0x7FFFFFFF % 360 = 127 is valid, the columns are not.
  const int32_t max_int = 0x7FFFFFFF;
  const VisibilityGridSpan max_span{max_int % 360, max_int % 360, max_int, max_int};
  CHECK(!VisibilityGridSpanNearTable(max_span));
  CHECK(Same(ClampVisibilityGridSpan(max_span), {127, 127, 255, 255}));

  // Only one corner far away is enough to clamp.
  CHECK(!VisibilityGridSpanNearTable({10, 20, 0, nan_int}));
  return check::Finish("visibility_grid_guard_tests");
}
