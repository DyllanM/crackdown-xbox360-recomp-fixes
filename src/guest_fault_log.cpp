#include "guest_fault_log.h"

#include <atomic>
#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/thread_state.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

extern PPCFuncMapping PPCFuncMappings[];

namespace crackdown {
namespace {

#if defined(_WIN32)
constexpr int kCrashLogLimit = 4;
constexpr uintptr_t kGuestSpan = uintptr_t(1) << 32;
constexpr uintptr_t kMaxFunctionSpan = 0x8000;

uintptr_t g_membase = 0;
std::atomic<int> g_logged{0};

// The generated function that starts closest below the host address.
bool GuestFunctionAt(uintptr_t host, uint32_t& guest, uintptr_t& start) {
  bool found = false;
  for (const PPCFuncMapping* m = PPCFuncMappings; m->guest != 0; ++m) {
    const uintptr_t candidate = reinterpret_cast<uintptr_t>(m->host);
    if (candidate <= host && host - candidate < kMaxFunctionSpan && (!found || candidate > start)) {
      found = true;
      start = candidate;
      guest = uint32_t(m->guest);
    }
  }
  return found;
}

LONG CALLBACK OnAccessViolation(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const uintptr_t host = record->ExceptionInformation[1];
  if (g_membase == 0 || host < g_membase || host - g_membase >= kGuestSpan || g_logged.load() >= kCrashLogLimit) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (g_logged.fetch_add(1) >= kCrashLogLimit) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const uint32_t guest_address = uint32_t(host - g_membase);
  const uintptr_t rip = info->ContextRecord->Rip;
  uint32_t function = 0;
  uintptr_t start = 0;
  const bool located = GuestFunctionAt(rip, function, start);
  const char* kind = record->ExceptionInformation[0] ? "write" : "read";
  if (const rex::runtime::ThreadState* thread = rex::runtime::ThreadState::Get(); thread && thread->context()) {
    const ::PPCContext& ctx = *thread->context();
    REXLOG_ERROR(
        "[GUEST_FAULT] Unhandled {} of guest 0x{:08X} rip=0x{:X} function=0x{:08X}{} thread=0x{:X} host_thread={} lr=0x{:08X} r1=0x{:08X} "
        "r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} r6=0x{:08X} r7=0x{:08X} r8=0x{:08X} r9=0x{:08X} r10=0x{:08X} "
        "r11=0x{:08X} r12=0x{:08X}",
        kind, guest_address, rip, function, located ? "" : " (unknown)", thread->thread_id(), GetCurrentThreadId(), uint32_t(ctx.lr),
        ctx.r1.u32, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32,
        ctx.r10.u32, ctx.r11.u32, ctx.r12.u32);
  } else {
    REXLOG_ERROR("[GUEST_FAULT] Unhandled {} of guest 0x{:08X} rip=0x{:X} function=0x{:08X} no guest thread state",
                 kind, guest_address, rip, function);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

}  // namespace

void InstallCrashDiagnostics(const uint8_t* membase) {
#if defined(_WIN32)
  g_membase = reinterpret_cast<uintptr_t>(membase);
  // Registered last, so it runs only for faults the runtime's own handler declined.
  AddVectoredExceptionHandler(0, OnAccessViolation);
#else
  (void)membase;
#endif
}

}  // namespace crackdown
