// The Direct3D fence wait blocks on the fence word instead of spinning; patch 0033 wakes it when the command processor
// writes guest memory.

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>

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
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#if defined(_WIN32) || defined(__linux__)

REX_EXTERN(__imp__sub_826BB4F0);
REX_EXTERN(__imp__sub_826BBEE0);

namespace {

// sub_826BB4F0(device, fence, type) spins on poll sub_826BBEE0 until the word at device+10768 reaches the fence; the
// poll's first word is the device. Other callers of the poll wait on other words and keep the guest's own polling.
constexpr uint32_t kPollSite = 0x826BB590;
constexpr uint32_t kDeviceFenceOffset = 10768;
constexpr uint32_t kDeviceFlagsOffset = 10813;
constexpr uint8_t kDeviceStoppedFlag = 4;
// Upper bound on one block, for fence writers that do not wake the waiter.
constexpr uint32_t kBlockTimeoutMs = 2;
// Blocks that ended by timeout with the word already changed before blocking is given up.
constexpr uint32_t kMissedWakeLimit = 8;

struct WaitState {
  uint32_t object = 0;
  uint32_t device = 0;
  uint32_t fence_address = 0;
  const uint32_t* word = nullptr;
  const uint32_t* guest_word = nullptr;
  uint32_t seen = 0;
};
thread_local WaitState t_wait;

std::atomic<bool> g_blocking_enabled{true};
std::atomic<uint32_t> g_missed_wakes{0};

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

// The command processor stores to and wakes physical_membase + (address & 0x1FFFFFFF), where address
// is the hardware form of the guest address that the guest emits into the packet.
bool ResolveFenceWord(uint8_t* base, uint32_t device, WaitState& wait) {
  if (!device || DeviceStopped(base, device)) {
    return false;
  }
  const uint32_t fence = LoadGuest32(base, device + kDeviceFenceOffset);
  if (!fence || (fence & 3)) {
    return false;
  }
  rex::system::KernelState* kernel = rex::system::kernel_state();
  rex::memory::Memory* memory = kernel ? kernel->memory() : nullptr;
  if (!memory) {
    return false;
  }
  const uint32_t physical = (fence & 0x1FFFFFFF) + ((((fence >> 20) & 0xFFF) + 512) & 0x1000);
  wait.word = memory->TranslatePhysical<const uint32_t*>(physical);
  wait.guest_word = rex::memory::GuestPtr<const uint32_t*>(base, fence);
  wait.device = device;
  wait.fence_address = fence;
  return ViewsAgree(wait.word, wait.guest_word);
}

enum class WaitOutcome { kReturned, kTimedOut, kFailed };

// Returns at once if the word no longer holds `undesired`; the comparison and the sleep are one
// atomic step, so a store between the caller's read and the sleep is never slept through.
WaitOutcome WaitWhileEqual(const uint32_t* word, uint32_t undesired) {
#if defined(_WIN32)
  if (WaitOnAddress(const_cast<uint32_t*>(word), &undesired, sizeof(undesired), kBlockTimeoutMs)) {
    return WaitOutcome::kReturned;
  }
  return GetLastError() == ERROR_TIMEOUT ? WaitOutcome::kTimedOut : WaitOutcome::kFailed;
#else
  const timespec timeout = {0, long(kBlockTimeoutMs) * 1000000L};
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

void BlockUntilFenceWritten(WaitState& wait) {
  const WaitOutcome outcome = WaitWhileEqual(wait.word, wait.seen);
  const uint32_t current = LoadWord(wait.word);
  if (outcome == WaitOutcome::kFailed || !ViewsAgree(wait.word, wait.guest_word)) {
    g_blocking_enabled.store(false, std::memory_order_relaxed);
  }
  if (outcome == WaitOutcome::kTimedOut && current != wait.seen) {
    if (g_missed_wakes.fetch_add(1, std::memory_order_relaxed) + 1 >= kMissedWakeLimit) {
      g_blocking_enabled.store(false, std::memory_order_relaxed);
    }
  } else if (outcome == WaitOutcome::kReturned && current != wait.seen) {
    g_missed_wakes.store(0, std::memory_order_relaxed);
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
// follows a check of the fence by the guest.
REX_HOOK_RAW(sub_826BBEE0) {
  const uint32_t object = ctx.r3.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_826BBEE0(ctx, base);
  if (caller != kPollSite || !ctx.r3.u32 || !g_blocking_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  WaitState& wait = t_wait;
  const uint32_t device = LoadGuest32(base, object);
  if (!wait.word || wait.object != object || wait.device != device) {
    wait = {};
    if (!ResolveFenceWord(base, device, wait)) {
      wait = {};
      return;
    }
    wait.object = object;
    wait.seen = LoadWord(wait.word);
    return;
  }
  if (DeviceStopped(base, device) || LoadGuest32(base, device + kDeviceFenceOffset) != wait.fence_address) {
    wait = {};
    return;
  }
  const uint32_t current = LoadWord(wait.word);
  if (current != wait.seen) {
    wait.seen = current;
    return;
  }
  BlockUntilFenceWritten(wait);
}

#endif  // _WIN32 || __linux__
