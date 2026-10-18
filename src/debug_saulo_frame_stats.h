#pragma once

// DEBUG_SAULO: frame-time statistics for the temporary performance report.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace crackdown::debug_saulo {

struct FrameTimeSummary {
  size_t frames = 0;
  double average_ms = 0;
  double p95_ms = 0;
  double p99_ms = 0;
  double worst_ms = 0;
  // Average rate of the slowest 1% of frames.
  double one_percent_low_fps = 0;
};

// Nearest-rank percentile of sorted values.
inline double Percentile(const std::vector<float>& sorted, double fraction) {
  const size_t rank = size_t(std::ceil(fraction * double(sorted.size())));
  return sorted[std::clamp<size_t>(rank, 1, sorted.size()) - 1];
}

// Sorts frame_ms.
inline FrameTimeSummary SummarizeFrameTimes(std::vector<float>& frame_ms) {
  FrameTimeSummary summary;
  summary.frames = frame_ms.size();
  if (frame_ms.empty()) {
    return summary;
  }
  std::sort(frame_ms.begin(), frame_ms.end());
  double sum = 0;
  for (const float ms : frame_ms) {
    sum += ms;
  }
  const size_t low_count = std::max<size_t>(1, frame_ms.size() / 100);
  double low_sum = 0;
  for (size_t i = frame_ms.size() - low_count; i < frame_ms.size(); ++i) {
    low_sum += frame_ms[i];
  }
  summary.average_ms = sum / double(frame_ms.size());
  summary.p95_ms = Percentile(frame_ms, 0.95);
  summary.p99_ms = Percentile(frame_ms, 0.99);
  summary.worst_ms = frame_ms.back();
  summary.one_percent_low_fps = low_sum > 0 ? 1000.0 * double(low_count) / low_sum : 0;
  return summary;
}

}  // namespace crackdown::debug_saulo
