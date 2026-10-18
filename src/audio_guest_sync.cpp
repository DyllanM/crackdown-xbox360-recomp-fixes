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
#include <rex/ppc.h>

#include "xaudio_barrier.h"

bool crackdown_audio_barrier_escape();

REX_EXTERN(__imp__sub_82A50000);
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
  uint64_t spins = 0;
  while (barrier.RoundOpen(round)) {
    if (++spins % kSpinsBeforeYield == 0) {
      if (crackdown_audio_barrier_escape()) {
        break;
      }
      std::this_thread::yield();
    } else {
      SpinPause();
    }
  }
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
    __imp__sub_82A50000(ctx, base);
    return;
  }
  const crackdown::XAudioBarrier::Arrival arrival = barrier->Arrive(processor, ExpectedProcessors(base, engine));
  if (!arrival.released_round) {
    WaitForRelease(*barrier, arrival.round);
  }
}

// sub_82A4FA70 is the job dispatcher's inline ResetEvent (`stw 0,4(r3)`), which never reaches the kernel, so the host
// event the runtime waits on stays signaled. Identical-code folding also puts this body in C++ vtables, where r3 is not
// an event: only events the runtime already shadows are reset on the host too.
REX_HOOK_RAW(sub_82A4FA70) {
  const uint32_t object = ctx.r3.u32;
  ctx.r11.s64 = 0;
  std::memset(rex::memory::GuestPtr<uint8_t*>(base, object + 4), 0, sizeof(uint32_t));
  if (LoadGuest8(base, object) > kSynchronizationEventType || LoadGuest32(base, object + 8) != kShadowedObjectSignature) {
    return;
  }
  __imp__KeResetEvent(ctx, base);
  ctx.r3.u64 = object;
  ctx.r11.s64 = 0;
}
