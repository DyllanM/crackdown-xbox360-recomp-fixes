// DEBUG_SAULO: frame pipeline diagnostics for the ~30 FPS limit. Appends to C:\decomp\saulo-frame30.log.

#include "debug_saulo_frame30.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <thread>

#include <fmt/format.h>

#include <bit>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// Mirrors include/rex/debug_saulo_frame.h of patch 0029-debug-saulo-frame-counters.
namespace rex::debug_saulo {
enum class FrameEvent : uint32_t {
  kVblank,
  kCommandProcessorSwap,
  kGuestOutputRefresh,
  kGuestOutputConsumed,
  kGuestOutputReplaced,
  kHostPresent,
  kCount,
};
uint64_t GetFrameEventCount(FrameEvent event);
}  // namespace rex::debug_saulo

namespace {

using rex::debug_saulo::FrameEvent;
using Clock = std::chrono::steady_clock;

constexpr const char* kLogDirectory = "C:\\decomp";
constexpr const char* kLogPath = "C:\\decomp\\saulo-frame30.log";
constexpr uint32_t kBootSampleLimit = 8;
constexpr uint32_t kPlaySampleLimit = 30;
constexpr uint32_t kFlushEverySamples = 5;
constexpr size_t kIntervalBuckets = 4;

uint64_t NowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}

std::atomic<uint64_t> g_vdswap_count{0};
std::atomic<uint64_t> g_guest_swap_count{0};
std::atomic<uint64_t> g_last_vdswap_vblank{0};
std::atomic<uint64_t> g_interval_buckets[kIntervalBuckets]{};
std::atomic<uint64_t> g_last_guest_swap_ns{0};
std::atomic<uint64_t> g_gap_sum_ns{0};
std::atomic<uint64_t> g_gap_count{0};
std::atomic<uint64_t> g_gap_min_ns{UINT64_MAX};
std::atomic<uint64_t> g_gap_max_ns{0};
std::atomic<uint64_t> g_duration_sum_ns{0};
std::atomic<uint64_t> g_duration_max_ns{0};
std::atomic<bool> g_gameplay{false};

// Fixed-step object pointer, its mode byte (+13), the limiter divisor and the device present interval field.
constexpr uint32_t kFrameObjectPointerAddress = 0x82DE25C0;
constexpr uint32_t kFixedStepModeOffset = 13;
constexpr uint32_t kLimiterDivisorAddress = 0x82BAA330;
constexpr uint32_t kDevicePresentIntervalOffset = 13220;
constexpr size_t kSchedulerBuckets = 3;
std::atomic<uint32_t> g_delta_bits{0};
std::atomic<int> g_target_fps{-1};
std::atomic<uint32_t> g_fixed_step{0};
std::atomic<uint32_t> g_limiter_divisor{0};
std::atomic<uint32_t> g_device_interval{0};
std::atomic<uint64_t> g_scheduler_intervals[kSchedulerBuckets]{};

uint32_t ReadGuestWord(uint8_t* base, uint32_t addr) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(value));
  return std::byteswap(value);
}

void RaiseMax(std::atomic<uint64_t>& target, uint64_t value) {
  uint64_t current = target.load(std::memory_order_relaxed);
  while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
  }
}

void LowerMin(std::atomic<uint64_t>& target, uint64_t value) {
  uint64_t current = target.load(std::memory_order_relaxed);
  while (value < current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
  }
}

struct Counts {
  uint64_t vblank, cp_swap, refresh, consumed, replaced, present, vdswap, guest_swap;
  uint64_t intervals[kIntervalBuckets];
};

Counts ReadCounts() {
  Counts c{};
  c.vblank = rex::debug_saulo::GetFrameEventCount(FrameEvent::kVblank);
  c.cp_swap = rex::debug_saulo::GetFrameEventCount(FrameEvent::kCommandProcessorSwap);
  c.refresh = rex::debug_saulo::GetFrameEventCount(FrameEvent::kGuestOutputRefresh);
  c.consumed = rex::debug_saulo::GetFrameEventCount(FrameEvent::kGuestOutputConsumed);
  c.replaced = rex::debug_saulo::GetFrameEventCount(FrameEvent::kGuestOutputReplaced);
  c.present = rex::debug_saulo::GetFrameEventCount(FrameEvent::kHostPresent);
  c.vdswap = g_vdswap_count.load(std::memory_order_relaxed);
  c.guest_swap = g_guest_swap_count.load(std::memory_order_relaxed);
  for (size_t i = 0; i < kIntervalBuckets; ++i) c.intervals[i] = g_interval_buckets[i].load(std::memory_order_relaxed);
  return c;
}

struct Totals {
  double seconds = 0;
  Counts counts{};
  double gap_sum_ms = 0, gap_count = 0, gap_min_ms = 1e9, gap_max_ms = 0;
  double duration_sum_ms = 0, duration_max_ms = 0;
};

bool Near(double value, double target) { return value > 0 && value >= target * 0.85 && value <= target * 1.15; }

const char* Diagnose(double vblank, double guest_swap, double vdswap, double cp_swap, double output, double present) {
  if (!Near(vblank, 60)) return "UNKNOWN";
  if (Near(guest_swap, 30) && Near(vdswap, 30) && Near(cp_swap, 30) && Near(output, 30)) return "TITLE_LIMITED_30";
  if (Near(guest_swap, 60) && Near(vdswap, 30)) return "VDSWAP_LIMITED_30";
  if (Near(vdswap, 60) && (Near(cp_swap, 30) || Near(output, 30) || Near(present, 30))) return "PRESENTER_LIMITED_30";
  if (Near(output, 60) && present > output * 1.5) return "HOST_PRESENT_REPEAT";
  return "UNKNOWN";
}

class Logger {
 public:
  void Start() {
    if (!OpenFile()) return;
    thread_ = std::jthread([this](std::stop_token stop) { Run(stop); });
  }

  void Stop() {
    if (!thread_.joinable()) return;
    thread_.request_stop();
    cv_.notify_all();
    thread_.join();
    std::lock_guard lock(mutex_);
    WriteSummary("shutdown");
    if (file_) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

 private:
  bool OpenFile() {
    std::error_code error;
    std::filesystem::create_directories(kLogDirectory, error);
#if defined(_WIN32)
    if (fopen_s(&file_, kLogPath, "ab")) file_ = nullptr;
#else
    file_ = std::fopen(kLogPath, "ab");
#endif
    if (!file_) {
      REXLOG_WARN("[DEBUG_SAULO][Performance] cannot open {}; frame diagnostics disabled", kLogPath);
      return false;
    }
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
    const unsigned long pid = GetCurrentProcessId();
#else
    localtime_r(&now, &local);
    const unsigned long pid = 0;
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    const std::string header = fmt::format(
        "\n============================================================\n"
        "[DEBUG_SAULO][Performance] RUN\ntimestamp={}\npid={}\n"
        "============================================================\n",
        stamp, pid);
    std::fputs(header.c_str(), file_);
    std::fflush(file_);
    return true;
  }

  void Run(std::stop_token stop) {
    Counts previous = ReadCounts();
    auto previous_time = Clock::now();
    std::mutex wait_mutex;
    std::unique_lock wait_lock(wait_mutex);
    while (!stop.stop_requested()) {
      cv_.wait_for(wait_lock, stop, std::chrono::seconds(1), [] { return false; });
      if (stop.stop_requested()) break;
      const Counts counts = ReadCounts();
      const auto now = Clock::now();
      const double dt = std::chrono::duration<double>(now - previous_time).count();
      std::lock_guard lock(mutex_);
      Sample(counts, previous, dt);
      previous = counts;
      previous_time = now;
    }
  }

  void Sample(const Counts& now, const Counts& before, double dt) {
    if (!file_ || summary_written_) return;
    const bool play = g_gameplay.load(std::memory_order_relaxed);
    uint32_t& samples = play ? play_samples_ : boot_samples_;
    if (samples >= (play ? kPlaySampleLimit : kBootSampleLimit)) return;
    ++samples;
    ++sec_;

    auto hz = [dt](uint64_t a, uint64_t b) { return double(a - b) / dt; };
    const double vblank = hz(now.vblank, before.vblank);
    const double guest_swap = hz(now.guest_swap, before.guest_swap);
    const double vdswap = hz(now.vdswap, before.vdswap);
    const double output = hz(now.refresh, before.refresh);
    const double present = hz(now.present, before.present);
    uint64_t ivl[kIntervalBuckets];
    for (size_t i = 0; i < kIntervalBuckets; ++i) ivl[i] = now.intervals[i] - before.intervals[i];

    const uint64_t gap_count = g_gap_count.exchange(0, std::memory_order_relaxed);
    const uint64_t gap_sum = g_gap_sum_ns.exchange(0, std::memory_order_relaxed);
    const uint64_t gap_min = g_gap_min_ns.exchange(UINT64_MAX, std::memory_order_relaxed);
    const uint64_t gap_max = g_gap_max_ns.exchange(0, std::memory_order_relaxed);
    const uint64_t duration_sum = g_duration_sum_ns.exchange(0, std::memory_order_relaxed);
    const uint64_t duration_max = g_duration_max_ns.exchange(0, std::memory_order_relaxed);
    const double gap_avg_ms = gap_count ? double(gap_sum) / gap_count / 1e6 : 0;
    const double gap_min_ms = gap_count ? double(gap_min) / 1e6 : 0;
    const double gap_max_ms = double(gap_max) / 1e6;
    const uint64_t swaps = now.guest_swap - before.guest_swap;
    const double duration_avg_ms = swaps ? double(duration_sum) / swaps / 1e6 : 0;

    const double delta = std::bit_cast<float>(g_delta_bits.load(std::memory_order_relaxed));
    fmt::print(file_,
               "[DEBUG_SAULO][Performance] phase={} sec={} dt={:.2f} vblank={:.1f} guest_swap={:.1f} vdswap={:.1f} "
               "cp_swap={:.1f} guest_output={:.1f} consumed={:.1f} replaced={:.1f} host_present={:.1f} "
               "ivl1={} ivl2={} ivl3={} ivl4+={} swap_ms={:.2f}/{:.2f}/{:.2f} swap_call_ms={:.2f}/{:.2f} "
               "delta={:.6f} fixed_step={} limiter_fps={} device_interval={:#x} sched_ivl={}/{}/{} "
               "target_fps={}\n",
               play ? "play" : "boot", sec_, dt, vblank, guest_swap, vdswap, hz(now.cp_swap, before.cp_swap),
               output, hz(now.consumed, before.consumed), hz(now.replaced, before.replaced), present, ivl[0], ivl[1],
               ivl[2], ivl[3], gap_avg_ms, gap_min_ms, gap_max_ms, duration_avg_ms, double(duration_max) / 1e6, delta,
               g_fixed_step.load(std::memory_order_relaxed), g_limiter_divisor.load(std::memory_order_relaxed),
               g_device_interval.load(std::memory_order_relaxed), g_scheduler_intervals[0].load(std::memory_order_relaxed),
               g_scheduler_intervals[1].load(std::memory_order_relaxed),
               g_scheduler_intervals[2].load(std::memory_order_relaxed), g_target_fps.load(std::memory_order_relaxed));

    Totals& totals = play ? play_ : boot_;
    totals.seconds += dt;
    AddCounts(totals.counts, now, before);
    totals.gap_sum_ms += double(gap_sum) / 1e6;
    totals.gap_count += double(gap_count);
    if (gap_count) totals.gap_min_ms = std::min(totals.gap_min_ms, gap_min_ms);
    totals.gap_max_ms = std::max(totals.gap_max_ms, gap_max_ms);
    totals.duration_sum_ms += double(duration_sum) / 1e6;
    totals.duration_max_ms = std::max(totals.duration_max_ms, double(duration_max) / 1e6);

    if (samples % kFlushEverySamples == 0) std::fflush(file_);
    if (play && samples >= kPlaySampleLimit) WriteSummary("sample limit");
  }

  static void AddCounts(Counts& total, const Counts& now, const Counts& before) {
    total.vblank += now.vblank - before.vblank;
    total.cp_swap += now.cp_swap - before.cp_swap;
    total.refresh += now.refresh - before.refresh;
    total.consumed += now.consumed - before.consumed;
    total.replaced += now.replaced - before.replaced;
    total.present += now.present - before.present;
    total.vdswap += now.vdswap - before.vdswap;
    total.guest_swap += now.guest_swap - before.guest_swap;
    for (size_t i = 0; i < kIntervalBuckets; ++i) total.intervals[i] += now.intervals[i] - before.intervals[i];
  }

  void WriteSummary(const char* reason) {
    if (!file_ || summary_written_) return;
    summary_written_ = true;
    const bool play = play_.seconds > 0;
    const Totals& t = play ? play_ : boot_;
    if (t.seconds <= 0) {
      std::fputs("[DEBUG_SAULO][Performance] SUMMARY no samples\n", file_);
      std::fflush(file_);
      return;
    }
    const double s = t.seconds;
    const double vblank = t.counts.vblank / s, guest_swap = t.counts.guest_swap / s, vdswap = t.counts.vdswap / s;
    const double cp_swap = t.counts.cp_swap / s, output = t.counts.refresh / s, present = t.counts.present / s;
    size_t dominant = 0;
    for (size_t i = 1; i < kIntervalBuckets; ++i) {
      if (t.counts.intervals[i] > t.counts.intervals[dominant]) dominant = i;
    }
    const double gap_avg = t.gap_count ? t.gap_sum_ms / t.gap_count : 0;
    const double duration_avg = t.counts.guest_swap ? t.duration_sum_ms / t.counts.guest_swap : 0;
    fmt::print(file_,
               "[DEBUG_SAULO][Performance] SUMMARY reason={} phase={} seconds={:.1f}\n"
               "  guest_vblank_hz={:.2f} guest_swap_hz={:.2f} vdswap_hz={:.2f} cp_swap_hz={:.2f}\n"
               "  guest_output_hz={:.2f} consumed_hz={:.2f} replaced_hz={:.2f} host_present_hz={:.2f}\n"
               "  dominant_vblank_interval={}{} (ivl1={} ivl2={} ivl3={} ivl4+={})\n"
               "  guest_swap_ms avg={:.2f} min={:.2f} max={:.2f}; swap_call_ms avg={:.2f} max={:.2f}\n"
               "  game_delta={:.6f} fixed_step={} limiter_fps={} device_interval={:#x} target_fps={}\n"
               "  diagnosis={}\n",
               reason, play ? "play" : "boot", s, vblank, guest_swap, vdswap, cp_swap, output, t.counts.consumed / s,
               t.counts.replaced / s, present, dominant + 1, dominant + 1 == kIntervalBuckets ? "+" : "",
               t.counts.intervals[0], t.counts.intervals[1], t.counts.intervals[2], t.counts.intervals[3], gap_avg,
               t.gap_count ? t.gap_min_ms : 0, t.gap_max_ms, duration_avg, t.duration_max_ms,
               std::bit_cast<float>(g_delta_bits.load(std::memory_order_relaxed)),
               g_fixed_step.load(std::memory_order_relaxed), g_limiter_divisor.load(std::memory_order_relaxed),
               g_device_interval.load(std::memory_order_relaxed), g_target_fps.load(std::memory_order_relaxed),
               Diagnose(vblank, guest_swap, vdswap, cp_swap, output, present));
    std::fflush(file_);
  }

  std::FILE* file_ = nullptr;
  std::jthread thread_;
  std::mutex mutex_;
  std::condition_variable_any cv_;
  uint32_t sec_ = 0, boot_samples_ = 0, play_samples_ = 0;
  bool summary_written_ = false;
  Totals boot_, play_;
};

Logger g_logger;

}  // namespace

void StartDebugSauloFrame30() { g_logger.Start(); }
void StopDebugSauloFrame30() { g_logger.Stop(); }
void DebugSauloFrame30GameplayStarted() { g_gameplay.store(true, std::memory_order_relaxed); }

void DebugSauloFrame30OnVdSwap() {
  g_vdswap_count.fetch_add(1, std::memory_order_relaxed);
  const uint64_t vblank = rex::debug_saulo::GetFrameEventCount(FrameEvent::kVblank);
  const uint64_t last = g_last_vdswap_vblank.exchange(vblank, std::memory_order_relaxed);
  if (last) {
    const uint64_t gap = vblank - last;
    g_interval_buckets[std::min<uint64_t>(gap ? gap - 1 : 0, kIntervalBuckets - 1)].fetch_add(1, std::memory_order_relaxed);
  }
}

// The only caller of VdSwap in the title: the Direct3D swap routine.
REX_EXTERN(__imp__sub_826BCA98);
REX_HOOK_RAW(sub_826BCA98) {
  const uint64_t start = NowNs();
  const uint64_t previous = g_last_guest_swap_ns.exchange(start, std::memory_order_relaxed);
  g_guest_swap_count.fetch_add(1, std::memory_order_relaxed);
  if (previous) {
    const uint64_t gap = start - previous;
    g_gap_sum_ns.fetch_add(gap, std::memory_order_relaxed);
    g_gap_count.fetch_add(1, std::memory_order_relaxed);
    LowerMin(g_gap_min_ns, gap);
    RaiseMax(g_gap_max_ns, gap);
  }
  const uint32_t frame_object = ReadGuestWord(base, kFrameObjectPointerAddress);
  if (frame_object) {
    g_fixed_step.store(*rex::memory::GuestPtr<const uint8_t*>(base, frame_object + kFixedStepModeOffset),
                       std::memory_order_relaxed);
  }
  g_limiter_divisor.store(ReadGuestWord(base, kLimiterDivisorAddress), std::memory_order_relaxed);
  g_device_interval.store(ReadGuestWord(base, ctx.r3.u32 + kDevicePresentIntervalOffset), std::memory_order_relaxed);
  __imp__sub_826BCA98(ctx, base);
  const uint64_t duration = NowNs() - start;
  g_duration_sum_ns.fetch_add(duration, std::memory_order_relaxed);
  RaiseMax(g_duration_max_ns, duration);
}

// Frame delta the title's getter returns, the step it feeds to its subsystems.
void DebugSauloFrame30OnDelta(float delta) {
  g_delta_bits.store(std::bit_cast<uint32_t>(delta), std::memory_order_relaxed);
}

void DebugSauloFrame30SetTargetFps(int fps) {
  g_target_fps.store(fps, std::memory_order_relaxed);
}

// Swap scheduler word: bits 8-11 are the present interval in vblanks, 0 meaning at once.
void DebugSauloFrame30OnSwapSchedule(uint32_t swap_word) {
  const uint32_t interval = (swap_word >> 8) & 0xF;
  g_scheduler_intervals[std::min<size_t>(interval, kSchedulerBuckets - 1)].fetch_add(1, std::memory_order_relaxed);
}
