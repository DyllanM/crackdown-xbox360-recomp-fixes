// Temporary DEBUG_SAULO diagnostics. Hooks call the original and only read validated guest memory.

#include "debug_saulo_diagnostics.h"

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

constexpr uint32_t kLogLimit = 64;
constexpr uint32_t kAchievementLogLimit = 32;
constexpr uint32_t kMaxPoolSlots = 1024;
constexpr uint32_t kXmaSlotSize = 96;
constexpr uint32_t kXmaSlotContext = 64;
constexpr uint32_t kGuestLowAddress = 0x10000;
constexpr uint32_t kGuestHighAddress = 0xFFFF0000;
// ReXGlue maps guest virtual address 0 at this host address.
constexpr uintptr_t kGuestVirtualHostBase = 0x100000000;
constexpr uintptr_t kGuestNullPageSize = 0x10000;

std::atomic<uint32_t> g_pool_fills{0};
std::atomic<uint32_t> g_pool_notable_fills{0};
std::atomic<uint32_t> g_pool_releases{0};
std::atomic<uint32_t> g_achievement_requests{0};
std::atomic<uint32_t> g_tile_reads{0};
std::atomic<uint32_t> g_null_faults{0};

uint32_t LoadBE32(const uint8_t* base, uint32_t addr) {
  uint32_t value;
  std::memcpy(&value, base + addr, sizeof(value));
  return std::byteswap(value);
}

bool IsPlausibleGuestRange(uint32_t addr, uint32_t size) {
  return addr >= kGuestLowAddress && uint64_t(addr) + size <= kGuestHighAddress;
}

struct XmaPool {
  bool valid = false;
  uint32_t slots = 0;
  uint32_t free_slots = 0;
};

// Title XMA voice pool: [+0] slot count, [+8] slot array, context pointer at slot+64 (0 = free).
XmaPool ScanXmaPool(const uint8_t* base, uint32_t pool) {
  XmaPool result;
  if (!pool) {
    return result;
  }
  result.slots = LoadBE32(base, pool);
  const uint32_t first = LoadBE32(base, pool + 8);
  if (result.slots > kMaxPoolSlots || !first) {
    return result;
  }
  for (uint32_t i = 0; i < result.slots; ++i) {
    if (LoadBE32(base, first + i * kXmaSlotSize + kXmaSlotContext) == 0) {
      ++result.free_slots;
    }
  }
  result.valid = true;
  return result;
}

// Item list at item+4 as used by sub_82440770: fields at +8 array, +12 capacity, +16 position,
// +20 count. The title reads element (count + position - 1, reduced once by capacity) as a
// pointer and dereferences it at offset 0 for its trace print.
struct AchievementListSnapshot {
  bool readable = false;
  bool index_in_range = false;
  uint32_t state = 0;
  uint32_t array = 0;
  uint32_t capacity = 0;
  uint32_t position = 0;
  uint32_t count = 0;
  uint32_t index = 0;
  uint32_t element = 0;
};

AchievementListSnapshot ReadAchievementList(const uint8_t* base, uint32_t item) {
  AchievementListSnapshot s;
  if (!IsPlausibleGuestRange(item, 96)) {
    return s;
  }
  s.readable = true;
  s.state = LoadBE32(base, item + 84);
  s.array = LoadBE32(base, item + 8);
  s.capacity = LoadBE32(base, item + 12);
  s.position = LoadBE32(base, item + 16);
  s.count = LoadBE32(base, item + 20);
  s.index = s.count + s.position - 1;
  if (s.capacity <= s.index) {
    s.index -= s.capacity;
  }
  s.index_in_range = s.capacity > 0 && s.capacity <= kMaxPoolSlots && s.index < s.capacity;
  if (s.index_in_range && IsPlausibleGuestRange(s.array, (s.index + 1) * 4)) {
    s.element = LoadBE32(base, s.array + s.index * 4);
  }
  return s;
}

#if defined(_WIN32)
LONG CALLBACK ReportNullGuestAccess(EXCEPTION_POINTERS* info) {
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
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

}  // namespace

void InstallDebugSauloFaultProbe() {
#if defined(_WIN32)
  AddVectoredExceptionHandler(1, ReportNullGuestAccess);
#endif
}

REX_EXTERN(__imp__sub_82A524F8);
REX_HOOK_RAW(sub_82A524F8) {
  const uint32_t pool = ctx.r3.u32;
  const XmaPool before = ScanXmaPool(base, pool);
  __imp__sub_82A524F8(ctx, base);
  if (!before.valid) {
    return;
  }
  const XmaPool after = ScanXmaPool(base, pool);
  const uint32_t fill = ++g_pool_fills;
  const bool no_slot_created = before.free_slots > 0 && after.free_slots == before.free_slots;
  const bool pool_full = before.free_slots > 0 && after.free_slots == 0;
  if (fill <= kLogLimit) {
    REXLOG_INFO("[DEBUG_SAULO][Audio] XMA pool fill #{}: slots={} free {} -> {}", fill, after.slots,
                before.free_slots, after.free_slots);
  } else if ((no_slot_created || pool_full) && g_pool_notable_fills++ < kLogLimit) {
    REXLOG_INFO("[DEBUG_SAULO][Audio] XMA pool fill #{} {}: slots={} free {} -> {}", fill,
                no_slot_created ? "created no context" : "filled pool", after.slots,
                before.free_slots, after.free_slots);
  }
}

REX_EXTERN(__imp__sub_82A51AA0);
REX_HOOK_RAW(sub_82A51AA0) {
  const uint32_t pool = ctx.r3.u32;
  const XmaPool before = ScanXmaPool(base, pool);
  __imp__sub_82A51AA0(ctx, base);
  if (!before.valid) {
    return;
  }
  const XmaPool after = ScanXmaPool(base, pool);
  const uint32_t release = ++g_pool_releases;
  if (release <= kLogLimit) {
    REXLOG_INFO("[DEBUG_SAULO][Audio] XMA pool release #{}: slots={} free {} -> {}", release,
                after.slots, before.free_slots, after.free_slots);
  }
}

REX_EXTERN(__imp__sub_82440770);
REX_HOOK_RAW(sub_82440770) {
  const uint32_t request = ++g_achievement_requests;
  if (request <= kAchievementLogLimit) {
    const AchievementListSnapshot s = ReadAchievementList(base, ctx.r3.u32);
    REXLOG_INFO(
        "[DEBUG_SAULO][Achievement] image request #{}: item={:08X} r4={:08X} readable={} state={} "
        "array={:08X} cap={} pos={} count={} index={} in_range={} element={:08X}",
        request, ctx.r3.u32, ctx.r4.u32, s.readable, s.state, s.array, s.capacity, s.position,
        s.count, s.index, s.index_in_range, s.element);
  }
  __imp__sub_82440770(ctx, base);
}

REX_EXTERN(__imp__sub_82744938);
REX_HOOK_RAW(sub_82744938) {
  const uint32_t read = ++g_tile_reads;
  const bool log = read <= kAchievementLogLimit;
  const uint32_t buffer = ctx.r7.u32;
  if (log) {
    REXLOG_INFO(
        "[DEBUG_SAULO][Achievement] tile read #{}: r3={:08X} r4={:08X} r5={:08X} r6={:08X} "
        "r7={:08X} r8={:08X} r9={:08X} r10={:08X}",
        read, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32,
        ctx.r10.u32);
  }
  __imp__sub_82744938(ctx, base);
  if (log) {
    const bool readable = IsPlausibleGuestRange(buffer, 4);
    REXLOG_INFO(
        "[DEBUG_SAULO][Achievement] tile read #{} status={:08X} first_word={:08X} readable={}",
        read, ctx.r3.u32, readable ? LoadBE32(base, buffer) : 0u, readable);
  }
}
