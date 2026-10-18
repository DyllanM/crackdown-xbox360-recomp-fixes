// DEBUG_SAULO: temporary performance report. Remove before release.

#include "debug_saulo_perf.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__linux__)
#include <dirent.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#endif

#include <rex/logging.h>

#include "debug_saulo_frame_stats.h"

namespace crackdown::debug_saulo {
namespace {

constexpr auto kReportInterval = std::chrono::seconds(5);
constexpr size_t kMaxFramesPerReport = 32768;
constexpr size_t kReportedThreads = 8;

// Never destroyed: guest threads and the report thread may still use them while static objects are destroyed at exit.
struct FrameLog {
  std::mutex mutex;
  std::vector<float> frame_ms;
  int64_t last_frame_ns = 0;
  std::atomic<uint64_t> repeated_presents{0};
};
FrameLog& Frames() {
  static FrameLog* frames = new FrameLog;
  return *frames;
}

using ThreadCpuSamples = std::unordered_map<uint64_t, std::pair<uint64_t, std::string>>;

struct ReportThread {
  std::mutex mutex;
  std::condition_variable wake;
  bool stop = false;
  std::thread thread;
  ThreadCpuSamples last_thread_cpu;
};
ReportThread& Report() {
  static ReportThread* report = new ReportThread;
  return *report;
}

struct ThreadCpu {
  uint64_t id;
  std::string name;
  double percent;
};

#if defined(_WIN32)

uint64_t FileTimeTicks(const FILETIME& time) {
  return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

std::string ThreadName(HANDLE thread) {
  PWSTR wide = nullptr;
  std::string name;
  if (SUCCEEDED(GetThreadDescription(thread, &wide)) && wide) {
    for (const wchar_t* c = wide; *c; ++c) {
      name.push_back(*c < 0x80 ? char(*c) : '?');
    }
    LocalFree(wide);
  }
  return name;
}

// CPU time in 100 ns units per thread of this process.
std::unordered_map<uint64_t, std::pair<uint64_t, std::string>> SampleThreadCpu() {
  std::unordered_map<uint64_t, std::pair<uint64_t, std::string>> samples;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return samples;
  }
  const DWORD process = GetCurrentProcessId();
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != process) {
      continue;
    }
    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
    if (!thread) {
      continue;
    }
    FILETIME creation, exit, kernel, user;
    if (GetThreadTimes(thread, &creation, &exit, &kernel, &user)) {
      samples[entry.th32ThreadID] = {FileTimeTicks(kernel) + FileTimeTicks(user), ThreadName(thread)};
    }
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
  return samples;
}

constexpr double kCpuTicksPerSecond = 1e7;

#elif defined(__linux__)

std::unordered_map<uint64_t, std::pair<uint64_t, std::string>> SampleThreadCpu() {
  std::unordered_map<uint64_t, std::pair<uint64_t, std::string>> samples;
  DIR* tasks = opendir("/proc/self/task");
  if (!tasks) {
    return samples;
  }
  while (const dirent* task = readdir(tasks)) {
    if (task->d_name[0] < '0' || task->d_name[0] > '9') {
      continue;
    }
    const std::string folder = std::string("/proc/self/task/") + task->d_name;
    std::ifstream stat_file(folder + "/stat");
    std::string stat((std::istreambuf_iterator<char>(stat_file)), std::istreambuf_iterator<char>());
    const size_t name_end = stat.rfind(')');
    if (name_end == std::string::npos) {
      continue;
    }
    std::istringstream fields(stat.substr(name_end + 2));
    std::string field;
    uint64_t utime = 0, stime = 0;
    for (int index = 3; fields >> field && index <= 15; ++index) {
      if (index == 14) utime = std::stoull(field);
      if (index == 15) stime = std::stoull(field);
    }
    std::ifstream comm_file(folder + "/comm");
    std::string name;
    std::getline(comm_file, name);
    samples[std::stoull(task->d_name)] = {utime + stime, name};
  }
  closedir(tasks);
  return samples;
}

const double kCpuTicksPerSecond = double(sysconf(_SC_CLK_TCK));

#else

std::unordered_map<uint64_t, std::pair<uint64_t, std::string>> SampleThreadCpu() { return {}; }
constexpr double kCpuTicksPerSecond = 1;

#endif

void LogThreadCpu(double window_seconds) {
  ThreadCpuSamples& last_samples = Report().last_thread_cpu;
  auto samples = SampleThreadCpu();
  std::vector<ThreadCpu> busiest;
  for (const auto& [id, sample] : samples) {
    const auto last = last_samples.find(id);
    if (last == last_samples.end()) {
      continue;
    }
    const uint64_t before = last->second.first;
    const double seconds = double(sample.first - std::min(before, sample.first)) / kCpuTicksPerSecond;
    busiest.push_back({id, sample.second, 100.0 * seconds / window_seconds});
  }
  last_samples = std::move(samples);
  std::sort(busiest.begin(), busiest.end(), [](const ThreadCpu& a, const ThreadCpu& b) { return a.percent > b.percent; });
  busiest.resize(std::min(busiest.size(), kReportedThreads));
  std::string line;
  for (const ThreadCpu& thread : busiest) {
    char item[160];
    std::snprintf(item, sizeof(item), " %s(%llu)=%.1f%%", thread.name.empty() ? "?" : thread.name.c_str(),
                  static_cast<unsigned long long>(thread.id), thread.percent);
    line += item;
  }
  REXLOG_INFO("[DEBUG_SAULO][PerfThreads] cpu_of_one_core:{}", line);
}

void LogFrameStats(double window_seconds) {
  std::vector<float> frame_ms;
  FrameLog& frames = Frames();
  {
    std::lock_guard lock(frames.mutex);
    frame_ms.assign(frames.frame_ms.begin(), frames.frame_ms.end());
    frames.frame_ms.clear();
  }
  const FrameTimeSummary summary = SummarizeFrameTimes(frame_ms);
  REXLOG_INFO(
      "[DEBUG_SAULO][PerfFrames] frames={} fps={:.1f} avg_ms={:.2f} p95_ms={:.2f} p99_ms={:.2f} worst_ms={:.2f} "
      "low1_fps={:.1f} repeated_presents={}",
      summary.frames, double(summary.frames) / window_seconds, summary.average_ms, summary.p95_ms, summary.p99_ms,
      summary.worst_ms, summary.one_percent_low_fps, frames.repeated_presents.exchange(0, std::memory_order_relaxed));
}

void ReportLoop() {
#if defined(_WIN32)
  SetThreadDescription(GetCurrentThread(), L"DEBUG_SAULO perf report");
#endif
  auto last = std::chrono::steady_clock::now();
  ReportThread& report = Report();
  report.last_thread_cpu = SampleThreadCpu();
  std::unique_lock lock(report.mutex);
  while (!report.wake.wait_for(lock, kReportInterval, [&report] { return report.stop; })) {
    lock.unlock();
    const auto now = std::chrono::steady_clock::now();
    const double window_seconds = std::chrono::duration<double>(now - last).count();
    last = now;
    LogFrameStats(window_seconds);
    LogMainLoopStats();
    LogTimingStats();
    LogFenceWaitStats();
    LogGuestAudioStats();
    LogThreadCpu(window_seconds);
    lock.lock();
  }
}

}  // namespace

int64_t PerfNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void RecordGuestFrame() {
  const int64_t now = PerfNowNs();
  FrameLog& frames = Frames();
  std::lock_guard lock(frames.mutex);
  if (frames.last_frame_ns && frames.frame_ms.size() < kMaxFramesPerReport) {
    frames.frame_ms.push_back(float(double(now - frames.last_frame_ns) * 1e-6));
  }
  frames.last_frame_ns = now;
}

void RecordRepeatedPresent() {
  Frames().repeated_presents.fetch_add(1, std::memory_order_relaxed);
}

void StartPerfReport() {
  ReportThread& report = Report();
  std::lock_guard lock(report.mutex);
  if (report.thread.joinable()) {
    return;
  }
  report.stop = false;
  {
    FrameLog& frames = Frames();
    std::lock_guard frames_lock(frames.mutex);
    frames.frame_ms.reserve(kMaxFramesPerReport);
  }
  report.thread = std::thread(ReportLoop);
}

void StopPerfReport() {
  ReportThread& report = Report();
  {
    std::lock_guard lock(report.mutex);
    report.stop = true;
  }
  report.wake.notify_all();
  if (report.thread.joinable()) {
    report.thread.join();
  }
}

}  // namespace crackdown::debug_saulo
