// Title audio synchronisation run on the host: the XAudio mix-worker rendezvous and the job dispatcher's inline
// ResetEvent. Both are adapted from Unowa001/crackdown-recomp (BSD 3-Clause, see THIRD-PARTY-LICENSES.txt).

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <thread>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "debug_saulo_perf.h"
#include "xaudio_barrier.h"

bool crackdown_audio_barrier_escape();

REX_EXTERN(__imp__sub_82A50000);
REX_EXTERN(__imp__sub_82A52648);
REX_EXTERN(__imp__KeResetEvent);

namespace {

constexpr uint32_t kEngineActiveOffset = 304;
constexpr uint32_t kEngineProcessorSlotsOffset = 308;
constexpr uint32_t kEngineProcessorSlots = 6;
constexpr uint32_t kPcrProcessorNumberOffset = 268;
constexpr uint32_t kSpinsBeforeYield = 4096;

// Guest KEVENT: type byte at +0, SignalState at +4; the runtime marks the ones it shadows with "REX\0" at +8.
constexpr uint32_t kShadowedObjectSignature = 0x52455800;
constexpr uint8_t kSynchronizationEventType = 1;

// DEBUG_SAULO: counters for the performance report.
struct GuestAudioStats {
  std::atomic<uint64_t> barrier_arrivals{0};
  std::atomic<uint64_t> barrier_waits{0};
  std::atomic<uint64_t> barrier_spins{0};
  std::atomic<uint64_t> barrier_yields{0};
  std::atomic<uint64_t> barrier_escapes{0};
  std::atomic<uint64_t> barrier_guest_fallbacks{0};
  std::atomic<uint64_t> xma_idle_waits{0};
  std::atomic<uint64_t> xma_idle_timeouts{0};
  std::atomic<uint64_t> xma_idle_ns{0};
  std::atomic<uint64_t> xma_idle_max_ns{0};
  std::atomic<uint64_t> event_resets{0};
  std::atomic<uint64_t> event_resets_mirrored{0};
};
GuestAudioStats g_stats;

uint32_t LoadGuest32(uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, address), sizeof(value));
  return std::byteswap(value);
}

uint8_t LoadGuest8(uint8_t* base, uint32_t address) {
  return *rex::memory::GuestPtr<const uint8_t*>(base, address);
}

void SpinPause() {
#if defined(_M_X64) || defined(__x86_64__)
  _mm_pause();
#endif
}

// XAudio has one engine with two barrier words that alternate per pass, so a few are enough.
struct BarrierSlot {
  std::atomic<uint32_t> word{0};
  crackdown::XAudioBarrier barrier;
};
BarrierSlot g_barriers[8];

crackdown::XAudioBarrier* BarrierFor(uint32_t word) {
  for (BarrierSlot& slot : g_barriers) {
    uint32_t current = slot.word.load(std::memory_order_acquire);
    if (current == 0 && slot.word.compare_exchange_strong(current, word, std::memory_order_acq_rel)) {
      current = word;
    }
    if (current == word) {
      return &slot.barrier;
    }
  }
  return nullptr;
}

uint64_t ExpectedProcessors(uint8_t* base, uint32_t engine) {
  uint64_t expected = 0;
  for (uint32_t i = 0; i < kEngineProcessorSlots; ++i) {
    if (LoadGuest32(base, engine + kEngineProcessorSlotsOffset + 4 * i)) {
      expected |= uint64_t(1) << i;
    }
  }
  return expected;
}

void WaitForRelease(const crackdown::XAudioBarrier& barrier, uint64_t round) {
  g_stats.barrier_waits.fetch_add(1, std::memory_order_relaxed);
  uint64_t spins = 0;
  while (barrier.RoundOpen(round)) {
    if (++spins % kSpinsBeforeYield == 0) {
      if (crackdown_audio_barrier_escape()) {
        g_stats.barrier_escapes.fetch_add(1, std::memory_order_relaxed);
        break;
      }
      g_stats.barrier_yields.fetch_add(1, std::memory_order_relaxed);
      std::this_thread::yield();
    } else {
      SpinPause();
    }
  }
  g_stats.barrier_spins.fetch_add(spins, std::memory_order_relaxed);
}

}  // namespace

// sub_82A50000(engine, word): the XAudio mix workers' rendezvous once per audio pass. The title's version marks its
// processor's byte in the guest word and spins until all are in or the word is reset, so a worker preempted in between
// can miss the reset and spin forever with its partner. Only this function uses the word.
REX_HOOK_RAW(sub_82A50000) {
  const uint32_t engine = ctx.r3.u32;
  if (!LoadGuest32(base, engine + kEngineActiveOffset)) {
    return;
  }
  const uint8_t processor = LoadGuest8(base, ctx.r13.u32 + kPcrProcessorNumberOffset);
  crackdown::XAudioBarrier* barrier = processor < 8 ? BarrierFor(ctx.r4.u32) : nullptr;
  if (!barrier) {
    g_stats.barrier_guest_fallbacks.fetch_add(1, std::memory_order_relaxed);
    __imp__sub_82A50000(ctx, base);
    return;
  }
  g_stats.barrier_arrivals.fetch_add(1, std::memory_order_relaxed);
  const crackdown::XAudioBarrier::Arrival arrival = barrier->Arrive(processor, ExpectedProcessors(base, engine));
  if (!arrival.released_round) {
    WaitForRelease(*barrier, arrival.round);
  }
}

// sub_82A52648(voices): waits up to 1/8 s for the XMA contexts to go idle, then resets them; returns 1 after a reset.
REX_HOOK_RAW(sub_82A52648) {
  const int64_t start = crackdown::debug_saulo::PerfNowNs();
  __imp__sub_82A52648(ctx, base);
  const uint64_t ns = uint64_t(crackdown::debug_saulo::PerfNowNs() - start);
  g_stats.xma_idle_waits.fetch_add(1, std::memory_order_relaxed);
  g_stats.xma_idle_ns.fetch_add(ns, std::memory_order_relaxed);
  if (ctx.r3.u32 == 1) {
    g_stats.xma_idle_timeouts.fetch_add(1, std::memory_order_relaxed);
  }
  uint64_t max = g_stats.xma_idle_max_ns.load(std::memory_order_relaxed);
  while (ns > max && !g_stats.xma_idle_max_ns.compare_exchange_weak(max, ns, std::memory_order_relaxed)) {
  }
}

// sub_82A4FA70 is the job dispatcher's inline ResetEvent (`stw 0,4(r3)`), which never reaches the kernel, so the host
// event the runtime waits on stays signaled. Identical-code folding also puts this body in C++ vtables, where r3 is not
// an event: only events the runtime already shadows are reset on the host too.
REX_HOOK_RAW(sub_82A4FA70) {
  const uint32_t object = ctx.r3.u32;
  ctx.r11.s64 = 0;
  std::memset(rex::memory::GuestPtr<uint8_t*>(base, object + 4), 0, sizeof(uint32_t));
  g_stats.event_resets.fetch_add(1, std::memory_order_relaxed);
  if (LoadGuest8(base, object) > kSynchronizationEventType || LoadGuest32(base, object + 8) != kShadowedObjectSignature) {
    return;
  }
  g_stats.event_resets_mirrored.fetch_add(1, std::memory_order_relaxed);
  __imp__KeResetEvent(ctx, base);
  ctx.r3.u64 = object;
  ctx.r11.s64 = 0;
}

void crackdown::debug_saulo::LogGuestAudioStats() {
  const uint64_t xma_waits = g_stats.xma_idle_waits.exchange(0, std::memory_order_relaxed);
  REXLOG_INFO(
      "[DEBUG_SAULO][PerfGuestAudio] barrier_arrivals={} barrier_waits={} barrier_spins={} barrier_yields={} "
      "barrier_escapes={} barrier_guest_fallbacks={} xma_idle_waits={} xma_idle_timeouts={} xma_idle_ms={:.2f} "
      "xma_idle_max_ms={:.2f} event_resets={} event_resets_mirrored={}",
      g_stats.barrier_arrivals.exchange(0, std::memory_order_relaxed),
      g_stats.barrier_waits.exchange(0, std::memory_order_relaxed),
      g_stats.barrier_spins.exchange(0, std::memory_order_relaxed),
      g_stats.barrier_yields.exchange(0, std::memory_order_relaxed),
      g_stats.barrier_escapes.exchange(0, std::memory_order_relaxed),
      g_stats.barrier_guest_fallbacks.exchange(0, std::memory_order_relaxed), xma_waits,
      g_stats.xma_idle_timeouts.exchange(0, std::memory_order_relaxed),
      double(g_stats.xma_idle_ns.exchange(0, std::memory_order_relaxed)) * 1e-6,
      double(g_stats.xma_idle_max_ns.exchange(0, std::memory_order_relaxed)) * 1e-6,
      g_stats.event_resets.exchange(0, std::memory_order_relaxed),
      g_stats.event_resets_mirrored.exchange(0, std::memory_order_relaxed));
}
