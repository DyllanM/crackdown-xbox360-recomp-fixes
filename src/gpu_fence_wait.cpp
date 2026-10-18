// The Direct3D GPU waits block on the word they poll instead of spinning; the command processor wakes them when it
// writes guest memory (patches 0033 and 0068).

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <ctime>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "debug_saulo_perf.h"

#if defined(_WIN32) || defined(__linux__)

REX_EXTERN(__imp__sub_826BB4F0);
REX_EXTERN(__imp__sub_826BBEE0);

namespace {

// sub_826BBEE0(wait) is the poll of every Direct3D GPU wait: the wait's first word is the device, its word at +20 the
// time base when the wait began, and the caller checks its own condition after each poll. The callers that wait on a
// word of the GPU writeback block at device+10768 block on it; the others keep the guest's own polling.
constexpr uint32_t kDeviceWritebackOffset = 10768;
constexpr uint32_t kDeviceFlagsOffset = 10813;
constexpr uint8_t kDeviceStoppedFlag = 4;
constexpr uint32_t kWaitStartOffset = 20;
// Blocks that ended by timeout with the word already changed before a site's blocking is given up.
constexpr uint32_t kMissedWakeLimit = 8;

struct PollSite {
  const char* name;
  uint32_t return_address;
  bool blocks;
  uint32_t word_offset;
  // Upper bound on one block, for writers that do not wake the waiter.
  uint32_t timeout_ms;
};

// fence: sub_826BB4F0, the fence written by EVENT_WRITE_SHD. ring_space: sub_826BAC90, the read pointer the command
// processor writes back (VdEnableRingBufferRPtrWriteBack gets block+60). ring_progress: sub_826BA678, the progress word
// at block+4. swap_throttle (sub_826BCA98) and resource (sub_826CD950) wait on words the CPU writes.
constexpr PollSite kSites[] = {
    {"fence", 0x826BB590, true, 0, 2},
    {"ring_space", 0x826BAD10, true, 60, 1},
    {"ring_progress", 0x826BA6EC, true, 4, 1},
    {"swap_throttle", 0x826BD160, false, 0, 0},
    {"resource", 0x826CD9C0, false, 0, 0},
};
constexpr size_t kSiteCount = std::size(kSites);
constexpr size_t kOtherSite = kSiteCount;

// DEBUG_SAULO: per-site counters for the performance report.
struct alignas(64) SiteStats {
  std::atomic<uint64_t> polls{0};
  std::atomic<uint64_t> blocks{0};
  std::atomic<uint64_t> woken{0};
  std::atomic<uint64_t> spurious{0};
  std::atomic<uint64_t> timeouts{0};
  std::atomic<uint64_t> missed{0};
  std::atomic<uint64_t> failed{0};
  std::atomic<uint64_t> blocked_ns{0};
  std::atomic<uint64_t> max_blocked_ns{0};
};
SiteStats g_stats[kSiteCount + 1];

struct alignas(64) SiteState {
  std::atomic<bool> blocking{true};
  std::atomic<uint32_t> missed_wakes{0};
};
SiteState g_site_state[kSiteCount];

struct WaitState {
  uint32_t object = 0;
  uint32_t device = 0;
  uint32_t start = 0;
  uint32_t block = 0;
  const uint32_t* word = nullptr;
  const uint32_t* guest_word = nullptr;
  uint32_t seen = 0;
};
thread_local WaitState t_wait;

size_t SiteIndex(uint32_t return_address) {
  for (size_t i = 0; i < kSiteCount; ++i) {
    if (kSites[i].return_address == return_address) {
      return i;
    }
  }
  return kOtherSite;
}

uint32_t LoadGuest32(uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, address), sizeof(value));
  return std::byteswap(value);
}

uint32_t LoadWord(const uint32_t* word) {
  return __atomic_load_n(word, __ATOMIC_ACQUIRE);
}

bool DeviceStopped(uint8_t* base, uint32_t device) {
  return *rex::memory::GuestPtr<const uint8_t*>(base, device + kDeviceFlagsOffset) & kDeviceStoppedFlag;
}

// Both views are the same pages; a difference that outlasts one re-read means the physical
// address is not the one the guest reads.
bool ViewsAgree(const uint32_t* physical_word, const uint32_t* guest_word) {
  const uint32_t physical = LoadWord(physical_word);
  return LoadWord(guest_word) == physical || LoadWord(physical_word) != physical;
}

void DisableBlocking(size_t site, const char* reason) {
  if (g_site_state[site].blocking.exchange(false, std::memory_order_relaxed)) {
    REXLOG_WARN("GPU wait {}: blocking disabled ({}), the guest polls again", kSites[site].name, reason);
  }
}

// The command processor stores to and wakes physical_membase + (address & 0x1FFFFFFF), where address
// is the hardware form of the guest address that the guest emits into the packet.
bool ResolveWaitWord(uint8_t* base, uint32_t device, uint32_t word_offset, WaitState& wait) {
  if (!device || DeviceStopped(base, device)) {
    return false;
  }
  const uint32_t block = LoadGuest32(base, device + kDeviceWritebackOffset);
  if (!block || (block & 3)) {
    return false;
  }
  rex::system::KernelState* kernel = rex::system::kernel_state();
  rex::memory::Memory* memory = kernel ? kernel->memory() : nullptr;
  if (!memory) {
    return false;
  }
  const uint32_t address = block + word_offset;
  const uint32_t physical = (address & 0x1FFFFFFF) + ((((address >> 20) & 0xFFF) + 512) & 0x1000);
  wait.word = memory->TranslatePhysical<const uint32_t*>(physical);
  wait.guest_word = rex::memory::GuestPtr<const uint32_t*>(base, address);
  wait.device = device;
  wait.block = block;
  return ViewsAgree(wait.word, wait.guest_word);
}

enum class WaitOutcome { kReturned, kTimedOut, kFailed };

// Returns at once if the word no longer holds `undesired`; the comparison and the sleep are one
// atomic step, so a store between the caller's read and the sleep is never slept through.
WaitOutcome WaitWhileEqual(const uint32_t* word, uint32_t undesired, uint32_t timeout_ms) {
#if defined(_WIN32)
  if (WaitOnAddress(const_cast<uint32_t*>(word), &undesired, sizeof(undesired), timeout_ms)) {
    return WaitOutcome::kReturned;
  }
  return GetLastError() == ERROR_TIMEOUT ? WaitOutcome::kTimedOut : WaitOutcome::kFailed;
#else
  const timespec timeout = {0, long(timeout_ms) * 1000000L};
  if (syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE, undesired, &timeout, nullptr, 0) == 0) {
    return WaitOutcome::kReturned;
  }
  switch (errno) {
    case EAGAIN:
    case EINTR:
      return WaitOutcome::kReturned;
    case ETIMEDOUT:
      return WaitOutcome::kTimedOut;
    default:
      return WaitOutcome::kFailed;
  }
#endif
}

void RecordBlockedTime(SiteStats& stats, uint64_t ns) {
  stats.blocked_ns.fetch_add(ns, std::memory_order_relaxed);
  uint64_t max = stats.max_blocked_ns.load(std::memory_order_relaxed);
  while (ns > max && !stats.max_blocked_ns.compare_exchange_weak(max, ns, std::memory_order_relaxed)) {
  }
}

void BlockUntilWordWritten(size_t site, WaitState& wait) {
  SiteStats& stats = g_stats[site];
  stats.blocks.fetch_add(1, std::memory_order_relaxed);
  const int64_t start = crackdown::debug_saulo::PerfNowNs();
  const WaitOutcome outcome = WaitWhileEqual(wait.word, wait.seen, kSites[site].timeout_ms);
  RecordBlockedTime(stats, uint64_t(crackdown::debug_saulo::PerfNowNs() - start));
  const uint32_t current = LoadWord(wait.word);
  if (outcome == WaitOutcome::kFailed) {
    stats.failed.fetch_add(1, std::memory_order_relaxed);
    DisableBlocking(site, "wait failed");
  } else if (!ViewsAgree(wait.word, wait.guest_word)) {
    DisableBlocking(site, "the guest reads another address");
  }
  SiteState& state = g_site_state[site];
  if (outcome == WaitOutcome::kTimedOut) {
    if (current != wait.seen) {
      stats.missed.fetch_add(1, std::memory_order_relaxed);
      if (state.missed_wakes.fetch_add(1, std::memory_order_relaxed) + 1 >= kMissedWakeLimit) {
        DisableBlocking(site, "its writer does not wake it");
      }
    } else {
      stats.timeouts.fetch_add(1, std::memory_order_relaxed);
    }
  } else if (outcome == WaitOutcome::kReturned) {
    if (current != wait.seen) {
      stats.woken.fetch_add(1, std::memory_order_relaxed);
      state.missed_wakes.store(0, std::memory_order_relaxed);
    } else {
      stats.spurious.fetch_add(1, std::memory_order_relaxed);
    }
  }
  wait.seen = current;
}

}  // namespace

REX_HOOK_RAW(sub_826BB4F0) {
  t_wait = {};
  __imp__sub_826BB4F0(ctx, base);
  t_wait = {};
}

// Returns nonzero while the wait should go on. The first call for a wait only reads the word, so a block always
// follows a check of the word by the guest.
REX_HOOK_RAW(sub_826BBEE0) {
  const uint32_t object = ctx.r3.u32;
  const size_t site = SiteIndex(uint32_t(ctx.lr));
  __imp__sub_826BBEE0(ctx, base);
  g_stats[site].polls.fetch_add(1, std::memory_order_relaxed);
  if (site == kOtherSite || !kSites[site].blocks || !ctx.r3.u32 ||
      !g_site_state[site].blocking.load(std::memory_order_relaxed)) {
    return;
  }
  WaitState& wait = t_wait;
  const uint32_t device = LoadGuest32(base, object);
  const uint32_t start = LoadGuest32(base, object + kWaitStartOffset);
  if (!wait.word || wait.object != object || wait.device != device || wait.start != start) {
    wait = {};
    if (!ResolveWaitWord(base, device, kSites[site].word_offset, wait)) {
      wait = {};
      return;
    }
    wait.object = object;
    wait.start = start;
    wait.seen = LoadWord(wait.word);
    return;
  }
  if (DeviceStopped(base, device) || LoadGuest32(base, device + kDeviceWritebackOffset) != wait.block) {
    wait = {};
    return;
  }
  const uint32_t current = LoadWord(wait.word);
  if (current != wait.seen) {
    wait.seen = current;
    return;
  }
  BlockUntilWordWritten(site, wait);
}

namespace crackdown::debug_saulo {

void LogFenceWaitStats() {
  std::string line;
  for (size_t i = 0; i <= kSiteCount; ++i) {
    SiteStats& stats = g_stats[i];
    const uint64_t polls = stats.polls.exchange(0, std::memory_order_relaxed);
    const uint64_t blocks = stats.blocks.exchange(0, std::memory_order_relaxed);
    const uint64_t woken = stats.woken.exchange(0, std::memory_order_relaxed);
    const uint64_t spurious = stats.spurious.exchange(0, std::memory_order_relaxed);
    const uint64_t timeouts = stats.timeouts.exchange(0, std::memory_order_relaxed);
    const uint64_t missed = stats.missed.exchange(0, std::memory_order_relaxed);
    const uint64_t failed = stats.failed.exchange(0, std::memory_order_relaxed);
    const uint64_t blocked_ns = stats.blocked_ns.exchange(0, std::memory_order_relaxed);
    const uint64_t max_ns = stats.max_blocked_ns.exchange(0, std::memory_order_relaxed);
    if (!polls) {
      continue;
    }
    const char* name = i < kSiteCount ? kSites[i].name : "other";
    const bool blocking = i < kSiteCount && kSites[i].blocks && g_site_state[i].blocking.load(std::memory_order_relaxed);
    line += fmt::format(" {}:polls={} blocks={} woken={} spurious={} timeouts={} missed={} failed={} blocked_ms={:.1f} "
                        "max_ms={:.2f} blocking={}",
                        name, polls, blocks, woken, spurious, timeouts, missed, failed, double(blocked_ns) * 1e-6,
                        double(max_ns) * 1e-6, blocking ? 1 : 0);
  }
  REXLOG_INFO("[DEBUG_SAULO][PerfGpuWait]{}", line.empty() ? " idle" : line);
}

}  // namespace crackdown::debug_saulo

#else

namespace crackdown::debug_saulo {
void LogFenceWaitStats() {}
}  // namespace crackdown::debug_saulo

#endif  // _WIN32 || __linux__
