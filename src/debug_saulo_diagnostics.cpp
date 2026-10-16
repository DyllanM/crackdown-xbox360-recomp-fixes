// Temporary DEBUG_SAULO runtime probes: guest null reads, HEAD draws, mixer divide and gameplay start.

#include "debug_saulo_diagnostics.h"
#include "debug_saulo_frame30.h"
#include "runtime_health.h"

#include <array>
#include <atomic>
#include <cmath>
#include <bit>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

constexpr uint32_t kGuestLowAddress = 0x10000;
constexpr uint32_t kGuestHighAddress = 0xFFFF0000;
// ReXGlue maps guest virtual address 0 at this host address.
constexpr uintptr_t kGuestVirtualHostBase = 0x100000000;
constexpr uintptr_t kGuestNullPageSize = 0x10000;
constexpr uint32_t kImageStart = 0x82000000;
constexpr uint32_t kImageEnd = 0x83670000;
constexpr uint32_t kFaultFrameLimit = 8;

[[maybe_unused]] thread_local bool t_diagnostic_read = false;
std::atomic<uint32_t> g_null_faults{0};
std::atomic<bool> g_gameplay_started{false};

bool IsPlausibleGuestRange(uint32_t addr, uint32_t size) {
  return addr >= kGuestLowAddress && uint64_t(addr) + size <= kGuestHighAddress;
}

uint32_t LoadGuestWord(uint8_t* base, uint32_t addr) {
  uint32_t value;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(value));
  return std::byteswap(value);
}

// Guest pages can be uncommitted, so reads use SEH; GuestPtr applies the 0xE0000000 host offset like guest code.
bool SafeReadWords(uint8_t* base, uint32_t addr, uint32_t count, uint32_t* out) {
  if (!base || !IsPlausibleGuestRange(addr, count * 4)) {
    return false;
  }
#if defined(_WIN32)
  __try {
    t_diagnostic_read = true;
    for (uint32_t i = 0; i < count; ++i) {
      out[i] = LoadGuestWord(base, addr + i * 4);
    }
    t_diagnostic_read = false;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    t_diagnostic_read = false;
    return false;
  }
#else
  for (uint32_t i = 0; i < count; ++i) {
    out[i] = LoadGuestWord(base, addr + i * 4);
  }
  return true;
#endif
}

uint32_t ReadWord(uint8_t* base, uint32_t addr) {
  uint32_t value = 0;
  SafeReadWords(base, addr, 1, &value);
  return value;
}

// Return addresses follow the title's frame layout: the caller's LR is saved at (caller sp - 8).
std::string GuestFrameText(uint8_t* base, uint32_t sp) {
  std::string text;
  for (uint32_t i = 0; i < kFaultFrameLimit; ++i) {
    uint32_t caller_sp = 0;
    uint32_t return_address = 0;
    if (!SafeReadWords(base, sp, 1, &caller_sp) || caller_sp <= sp ||
        !SafeReadWords(base, caller_sp - 8, 1, &return_address) || return_address < kImageStart ||
        return_address >= kImageEnd) {
      break;
    }
    text += fmt::format("{}{:08X}", i ? "," : "", return_address);
    sp = caller_sp;
  }
  return text.empty() ? std::string("none") : text;
}

#if defined(_WIN32)
void LogFaultGuestContext() {
  const rex::runtime::ThreadState* thread = rex::runtime::ThreadState::Get();
  if (!thread || !thread->context()) {
    return;
  }
  const PPCContext& ctx = *thread->context();
  auto* base = reinterpret_cast<uint8_t*>(kGuestVirtualHostBase);
  REXLOG_ERROR(
      "[DEBUG_SAULO][Fault] guest lr={:08X} r1={:08X} r3={:08X} r4={:08X} r5={:08X} r6={:08X} r7={:08X} r8={:08X} r9={:08X} r10={:08X} guest_thread={} r29={:08X} r30={:08X} r31={:08X} "
      "frames=[{}]",
      static_cast<uint32_t>(ctx.lr), ctx.r1.u32, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32, thread->thread_id(), ctx.r29.u32, ctx.r30.u32, ctx.r31.u32,
      GuestFrameText(base, ctx.r1.u32));
}

LONG CALLBACK ReportNullGuestAccess(EXCEPTION_POINTERS* info) {
  if (t_diagnostic_read) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const uintptr_t fault = record->ExceptionInformation[1];
  if (fault < kGuestVirtualHostBase || fault - kGuestVirtualHostBase >= kGuestNullPageSize) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (g_null_faults++ < 8) {
    const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    REXLOG_ERROR("[DEBUG_SAULO][Fault] {} of guest 0x{:08X} rip_rva=0x{:X} os_thread={}",
                 record->ExceptionInformation[0] ? "write" : "read",
                 static_cast<uint32_t>(fault - kGuestVirtualHostBase),
                 info->ContextRecord->Rip - module, GetCurrentThreadId());
    LogFaultGuestContext();
    rex::ObserveRuntime("FAULT", "guest_fault", fault - kGuestVirtualHostBase,
                        record->ExceptionInformation[0], info->ContextRecord->Rip - module);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

}  // namespace

void InstallDebugSauloFaultProbe() {
  REXLOG_INFO("[DEBUG_SAULO][DiagInit] capture=guest_null_access");
#if defined(_WIN32)
  AddVectoredExceptionHandler(1, ReportNullGuestAccess);
#endif
}

// DEBUG_SAULO: bounded combined capture for HEAD draws and the mixer divide. Remove after the run.
constexpr uint32_t kHeadDrawEventLimit = 4;
constexpr uint32_t kBodyDrawEventLimit = 2;
constexpr uint32_t kMixDivideEventLimit = 8;
constexpr uint32_t kMixDivideCallSite = 0x82A59D30;
constexpr uint32_t kDrawKindGlobal = 0x82D826E4;
std::atomic<uint32_t> g_body_draw_events{0};
std::atomic<uint32_t> g_head_draw_events{0};
std::atomic<uint32_t> g_mix_divide_events{0};

std::string WordsText(uint8_t* base, uint32_t addr, uint32_t count) {
  std::array<uint32_t, 16> words{};
  if (!SafeReadWords(base, addr, count, words.data())) return "unreadable";
  std::string text;
  for (uint32_t i = 0; i < count; ++i) text += fmt::format("{}{:08X}", i ? "," : "", words[i]);
  return text;
}

uint32_t FloatBits(double value) { return std::bit_cast<uint32_t>(static_cast<float>(value)); }

void LogDraw(const char* path, uint8_t* base, uint32_t kind, uint32_t state, uint32_t world,
             uint32_t mix_world, uint32_t resource) {
  REXLOG_ERROR(
      "[DEBUG_SAULO][HeadDraw] path={} kind={} state={:08X} world={:08X} world_words=[{}] "
      "mix_world={:08X} mix_words=[{}] resource={:08X} resource_words=[{}] palette8_candidate=[{}]",
      path, kind, state, world, WordsText(base, world, 16), mix_world,
      mix_world ? WordsText(base, mix_world, 16) : std::string("none"), resource,
      WordsText(base, resource, 16), WordsText(base, resource + 512, 16));
}

REX_EXTERN(__imp__sub_823B8B78);
REX_HOOK_RAW(sub_823B8B78) {
  if (g_body_draw_events.fetch_add(1) < kBodyDrawEventLimit) {
    LogDraw("body", base, ReadWord(base, kDrawKindGlobal), ctx.r3.u32, ctx.r6.u32, 0, ctx.r7.u32);
  }
  __imp__sub_823B8B78(ctx, base);
}

REX_EXTERN(__imp__sub_823B8F38);
REX_HOOK_RAW(sub_823B8F38) {
  if (g_head_draw_events.fetch_add(1) < kHeadDrawEventLimit) {
    LogDraw("head", base, ReadWord(base, kDrawKindGlobal), ctx.r3.u32, ctx.r6.u32, ctx.r9.u32,
            ctx.r7.u32);
  }
  __imp__sub_823B8F38(ctx, base);
}

REX_EXTERN(__imp__sub_82AB57F8);
REX_HOOK_RAW(sub_82AB57F8) {
  const float sum_half = float(ctx.f12.f64);
  const float numerator = float(ctx.f13.f64);
  const float quotient_1 = float(ctx.f0.f64);
  const float divisor_2 = float(ctx.f2.f64);
  const bool invalid = divisor_2 == 0.0f || !std::isfinite(divisor_2) || !std::isfinite(quotient_1) ||
                       !std::isfinite(numerator) || !std::isfinite(sum_half);
  if (uint32_t(ctx.lr) == kMixDivideCallSite && invalid &&
      g_mix_divide_events.fetch_add(1) < kMixDivideEventLimit) {
    REXLOG_ERROR(
        "[DEBUG_SAULO][MixDivide] sum_half={:08X} numerator={:08X} quotient_1={:08X} divisor_2={:08X} "
        "state_r29={:08X} state_r30={:08X} frames=[{}]",
        FloatBits(sum_half), FloatBits(numerator), FloatBits(quotient_1), FloatBits(divisor_2),
        ctx.r29.u32, ctx.r30.u32, GuestFrameText(base, ctx.r1.u32));
  }
  __imp__sub_82AB57F8(ctx, base);
}

// The first HUD gather of a session marks gameplay start for the runtime health observer.
REX_EXTERN(__imp__sub_823FD138);
REX_HOOK_RAW(sub_823FD138) {
  if (!g_gameplay_started.exchange(true)) {
    RuntimeHealthGameplayStarted();
    DebugSauloFrame30GameplayStarted();
  }
  __imp__sub_823FD138(ctx, base);
}
