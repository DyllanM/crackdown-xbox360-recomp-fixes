#include "massive_server_guard.h"

#include <atomic>

#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_memory.h"

// Codegen hook before the `lwz r30,100(r1)` at 82AA0A6C in sub_82AA0920 (config/overrides.toml): r1+100 holds the
// count XamEnumerate returned, and the loop reloads it on every pass.
void crackdown_massive_server_count_guard(PPCRegister& stack) {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t address = stack.u32 + 100;
  const uint32_t count = crackdown::guest::Load32(base, address);
  const uint32_t safe_count = crackdown::ClampMassiveServerCount(count);
  if (safe_count == count) {
    return;
  }
  crackdown::guest::Store32(base, address, safe_count);
  static std::atomic_flag logged;
  if (!logged.test_and_set()) {
    REXLOG_WARN("[XAM_GUARD] Clamped enumeration count from 0x{:08X} to {}", count, safe_count);
  }
}
