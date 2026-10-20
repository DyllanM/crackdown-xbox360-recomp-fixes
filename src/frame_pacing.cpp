// Frame rate: above 30 the title runs its variable-step mode (its 1/60 s, 16 ms frame path) with a step of one
// frame of the selected rate instead of the fixed 1/30 s step it ships with. 30 is the shipped timing.

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "frame_pacing.h"
#include "frame_timing.h"
#include "settings.h"

REX_EXTERN(__imp__sub_826A7698);
REX_EXTERN(__imp__sub_826A6B50);
REX_EXTERN(__imp__sub_82324938);
REX_EXTERN(__imp__sub_826B1150);
REX_EXTERN(__imp__sub_826BC568);
REX_EXTERN(__imp__sub_826BC3D0);

namespace {

using crackdown::FrameStep;

constexpr uint32_t kOriginalPresentInterval = crackdown::PresentIntervalFor(crackdown::kOriginalFrameRate);

// Frame delta getter's time scale (float, 1.0 unless the game scales time).
constexpr uint32_t kTimeScaleAddress = 0x82C63578;

constexpr uint32_t kTimingObjectAddress = 0x82DE25C0;
constexpr uint32_t kFixedStepOffset = 13;
constexpr uint32_t kSuspendedOffset = 14;
constexpr uint32_t kLogicalTickOffset = 16;
constexpr uint32_t kLimiterFrameRateAddress = 0x82BAA330;
constexpr uint32_t kDevicePresentIntervalOffset = 13220;
// The swap word: front buffer | present interval << 8 | immediate threshold. A swap whose vblank has passed is
// released at once below the threshold percentage of a frame.
constexpr uint32_t kSwapImmediateThresholdMask = 0xFF;
constexpr uint32_t kSwapPresentIntervalMask = 0xF00;
constexpr uint32_t kSwapImmediateAlways = 100;
// The variable-step frame time the title's inline sites use without the replay branch.
constexpr uint32_t kVariableStepFrameMs = 16;

std::atomic<uint32_t> g_step_bits{std::bit_cast<uint32_t>(1.0f / 60.0f)};
std::atomic<uint32_t> g_frame_ms{kVariableStepFrameMs};
std::atomic<double> g_original_steps{crackdown::FixedFrameStep(crackdown::kDefaultFrameRate).original_steps};

// Only the main thread runs frames.
FrameStep g_step = crackdown::FixedFrameStep(crackdown::kDefaultFrameRate);
crackdown::FramePacer g_pacer;
crackdown::CompensatedStepClock g_console_clock;
int g_console_rate = 0;
int64_t g_console_last_ns = 0;
double g_logical_tick_fraction = 0.0;
double g_frame_ms_fraction = 0.0;
int g_logged_rate = -1;

// 30 and 60 both present at the guest vblank (no host pacing), so a slow frame already shows up as a longer
// measured interval here; resets the clock on a rate change so switching rates never carries over stale debt.
crackdown::FrameStep ConsolePacedStep(int rate, double interval) {
  if (rate != g_console_rate) {
    g_console_rate = rate;
    g_console_clock.Reset();
    return crackdown::FixedFrameStep(rate);
  }
  return crackdown::StepFromSeconds(g_console_clock.Take(interval, 1.0 / rate).seconds);
}

// Set by the launcher before the title starts.
int FrameRate() {
  return crackdown::NormalizeFrameRate(REXCVAR_GET(crackdown_frame_rate));
}

bool HighFrameRate(int rate) {
  return rate != crackdown::kOriginalFrameRate;
}

uint32_t LoadBE32(uint8_t* base, uint32_t addr) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(value));
  return std::byteswap(value);
}

void StoreBE32(uint8_t* base, uint32_t addr, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &value, sizeof(value));
}

float LoadBEFloat(uint8_t* base, uint32_t addr) {
  return std::bit_cast<float>(LoadBE32(base, addr));
}

uint8_t* GuestByte(uint8_t* base, uint32_t addr) {
  return rex::memory::GuestPtr<uint8_t*>(base, addr);
}

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Sleeps until shortly before the deadline, then yields until it, so a coarse timer cannot make the frame late.
void WaitUntil(int64_t deadline_ns) {
  constexpr int64_t kYieldNs = 500'000;
  const int64_t remaining = deadline_ns - NowNs();
  if (remaining > kYieldNs) {
#if defined(_WIN32)
    thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                       TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    due.QuadPart = -(remaining - kYieldNs) / 100;
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
      WaitForSingleObject(timer, INFINITE);
    } else {
      Sleep(DWORD((remaining - kYieldNs) / 1'000'000));
    }
#else
    std::this_thread::sleep_until(std::chrono::steady_clock::time_point(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::nanoseconds(deadline_ns - kYieldNs))));
#endif
  }
  while (NowNs() < deadline_ns) {
    std::this_thread::yield();
  }
}

// The timing object can be replaced while the title runs, and every new one starts in fixed-step mode.
void SelectVariableStep(uint8_t* base, int rate) {
  if (const uint32_t timing = LoadBE32(base, kTimingObjectAddress)) {
    *GuestByte(base, timing + kFixedStepOffset) = 0;
  }
  const uint32_t limiter = crackdown::LimiterFrameRateFor(rate);
  if (LoadBE32(base, kLimiterFrameRateAddress) != limiter) {
    StoreBE32(base, kLimiterFrameRateAddress, limiter);
  }
}

// Picks this frame's step; host-paced rates first wait for their release time, 30 and 60 measure the real
// interval since they are already paced by the guest vblank.
void BeginFrame(int rate) {
  const int64_t now = NowNs();
  if (crackdown::HostPacedFrameRate(rate)) {
    const crackdown::FramePacer::Frame frame = g_pacer.Next(rate, now);
    WaitUntil(frame.release_ns);
    g_step = frame.step;
    g_frame_ms_fraction += g_step.seconds * 1000.0;
    g_frame_ms.store(crackdown::TakeWhole(g_frame_ms_fraction), std::memory_order_relaxed);
  } else {
    const double interval = g_console_last_ns ? double(now - g_console_last_ns) * 1e-9 : 0.0;
    g_console_last_ns = now;
    g_step = ConsolePacedStep(rate, interval);
  }
  g_step_bits.store(std::bit_cast<uint32_t>(g_step.seconds_f), std::memory_order_relaxed);
  g_original_steps.store(g_step.original_steps, std::memory_order_relaxed);
}

// +16 counts the title's 1/30 s steps. The original advances it unconditionally while not suspended; this tops
// that up to the compensated step so a missed frame's extra elapsed time still reaches it, without double-counting
// what the original already added. The fraction never goes negative: a tick already granted is never clawed back.
void SyncLogicalTick(uint8_t* base, uint32_t timing, uint32_t applied) {
  g_logical_tick_fraction = std::max(0.0, g_logical_tick_fraction + g_step.original_steps - double(applied));
  if (const uint32_t whole = crackdown::TakeWhole(g_logical_tick_fraction)) {
    StoreBE32(base, timing + kLogicalTickOffset, LoadBE32(base, timing + kLogicalTickOffset) + whole);
  }
}

uint32_t TitlePresentInterval(uint32_t interval) {
  return HighFrameRate(FrameRate()) && interval == kOriginalPresentInterval ? 1 : interval;
}

void LogFrameRate(int rate) {
  if (rate == g_logged_rate) {
    return;
  }
  g_logged_rate = rate;
  if (rate == crackdown::kFrameRateUnlimited) {
    REXLOG_INFO("Frame rate: unlimited, game time follows the measured frame time");
  } else if (crackdown::HostPacedFrameRate(rate)) {
    REXLOG_INFO("Frame rate: {} FPS, paced on the host", rate);
  } else {
    REXLOG_INFO("Frame rate: {} FPS, {} vblank(s) per frame", rate, crackdown::PresentIntervalFor(rate));
  }
}

}  // namespace

// One main-loop frame. It reads the fixed-step byte for the frame time, the frame limiter and the logical tick.
REX_HOOK_RAW(sub_826A7698) {
  const int rate = FrameRate();
  LogFrameRate(rate);
  BeginFrame(rate);
  if (HighFrameRate(rate)) {
    SelectVariableStep(base, rate);
  }
  __imp__sub_826A7698(ctx, base);
}

// The simulation update. Its only caller passes 1/30 s times the time scale; g_step.original_steps is exactly 1.0
// on an on-time frame at every rate, so this is a no-op multiply unless a frame ran long or short of its target.
REX_HOOK_RAW(sub_826A6B50) {
  ctx.f1.f64 = double(float(ctx.f1.f64 * g_step.original_steps));
  __imp__sub_826A6B50(ctx, base);
}

// The frame delta getter. With the fixed step on (30), the original always returns timescale/30, the same value
// g_step gives on an on-time frame, so this can replace it outright instead of only overriding the variable-step
// rates; it is what keeps camera and animation on the same compensated clock as the simulation update.
REX_HOOK_RAW(sub_8254C568) {
  ctx.f1.f64 =
      double(LoadBEFloat(base, kTimeScaleAddress) * std::bit_cast<float>(g_step_bits.load(std::memory_order_relaxed)));
}

REX_HOOK_RAW(sub_82324938) {
  const uint32_t timing = LoadBE32(base, kTimingObjectAddress);
  const bool active = timing && !*GuestByte(base, timing + kSuspendedOffset);
  const uint32_t before = active ? LoadBE32(base, timing + kLogicalTickOffset) : 0;
  __imp__sub_82324938(ctx, base);
  if (active) {
    SyncLogicalTick(base, timing, LoadBE32(base, timing + kLogicalTickOffset) - before);
  }
}

// D3D present interval setter: r3 = device, r4 = D3DPRESENT_INTERVAL value.
REX_HOOK_RAW(sub_826B1150) {
  ctx.r4.u64 = TitlePresentInterval(ctx.r4.u32);
  __imp__sub_826B1150(ctx, base);
}

// Builds the swap packet from the device's interval; the interval from device creation never goes through the setter.
REX_HOOK_RAW(sub_826BC568) {
  const uint32_t field = ctx.r3.u32 + kDevicePresentIntervalOffset;
  const uint32_t interval = LoadBE32(base, field);
  if (const uint32_t wanted = TitlePresentInterval(interval); wanted != interval) {
    StoreBE32(base, field, wanted);
  }
  __imp__sub_826BC568(ctx, base);
}

// The XDK swap callback: r3 = the swap word. The title passes threshold 0, so a frame finished just after its vblank
// waited a whole extra vblank; the host presents without tearing either way. Host-paced rates take interval 0.
REX_HOOK_RAW(sub_826BC3D0) {
  const int rate = FrameRate();
  if (crackdown::HostPacedFrameRate(rate)) {
    ctx.r3.u64 = ctx.r3.u32 & ~kSwapPresentIntervalMask;
  } else if (HighFrameRate(rate)) {
    ctx.r3.u64 = (ctx.r3.u32 & ~kSwapImmediateThresholdMask) | kSwapImmediateAlways;
  }
  __imp__sub_826BC3D0(ctx, base);
}

// Codegen midasm hook after each inline `li rN,16`: the 16 ms frame time of the title's variable-step sites.
void crackdown_variable_step_frame_ms(PPCRegister& frame_ms) {
  if (crackdown::HostPacedFrameRate(FrameRate())) {
    frame_ms.u64 = g_frame_ms.load(std::memory_order_relaxed);
  }
}

// Codegen midasm hook after the `fdivs f0,f0,f20` at 822B5634 in sub_822B4D40: f0 is the step-down snap's downward
// velocity, f20 the step's dt, and the next instructions negate f0 into out.y.
void crackdown_ground_snap_velocity(PPCRegister& velocity, PPCRegister& dt) {
  velocity.f64 = double(crackdown::GroundSnapVelocity(float(velocity.f64), float(dt.f64)));
}

double crackdown::OriginalStepsPerUpdate() {
  return g_original_steps.load(std::memory_order_relaxed);
}
