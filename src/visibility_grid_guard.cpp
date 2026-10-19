#include "visibility_grid_guard.h"

#include <atomic>

#include <rex/logging.h>
#include <rex/ppc.h>

// Codegen hook before the `srawi r5,r11,3` at 822541D8 in sub_82252150 (config/overrides.toml): r27 and r29 hold the
// first and last rows, r11 and r10 the first and last columns, before any of them indexes the grid.
void crackdown_visibility_grid_guard(PPCRegister& first_column, PPCRegister& last_column, PPCRegister& first_row,
                                     PPCRegister& last_row) {
  const crackdown::VisibilityGridSpan span{static_cast<int32_t>(first_row.u32), static_cast<int32_t>(last_row.u32),
                                           static_cast<int32_t>(first_column.u32),
                                           static_cast<int32_t>(last_column.u32)};
  if (crackdown::VisibilityGridSpanNearTable(span)) {
    return;
  }
  const crackdown::VisibilityGridSpan safe = crackdown::ClampVisibilityGridSpan(span);
  first_row.u64 = static_cast<uint32_t>(safe.first_row);
  last_row.u64 = static_cast<uint32_t>(safe.last_row);
  first_column.u64 = static_cast<uint32_t>(safe.first_column);
  last_column.u64 = static_cast<uint32_t>(safe.last_column);
  static std::atomic_flag logged;
  if (!logged.test_and_set()) {
    REXLOG_WARN("[GRID_GUARD] Clamped audio grid rows {}..{} columns {}..{}: the read would leave the table",
                span.first_row, span.last_row, span.first_column, span.last_column);
  }
}
