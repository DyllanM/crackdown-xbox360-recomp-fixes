#include <vector>

#include "check.h"
#include "debug_saulo_frame_stats.h"

using namespace crackdown::debug_saulo;

namespace {

void TestEmpty() {
  std::vector<float> frames;
  const FrameTimeSummary summary = SummarizeFrameTimes(frames);
  CHECK(summary.frames == 0 && summary.average_ms == 0 && summary.one_percent_low_fps == 0);
}

void TestSteadyFrames() {
  std::vector<float> frames(600, 16.0f);
  const FrameTimeSummary summary = SummarizeFrameTimes(frames);
  CHECK(summary.frames == 600);
  CHECK(summary.average_ms == 16.0 && summary.p95_ms == 16.0 && summary.p99_ms == 16.0 && summary.worst_ms == 16.0);
  CHECK(summary.one_percent_low_fps == 62.5);
}

void TestHitches() {
  std::vector<float> frames(100, 10.0f);
  frames[3] = 50.0f;
  frames[70] = 30.0f;
  const FrameTimeSummary summary = SummarizeFrameTimes(frames);
  CHECK(summary.worst_ms == 50.0);
  CHECK(summary.p99_ms == 30.0);
  CHECK(summary.p95_ms == 10.0);
  // The slowest 1% of 100 frames is the single 50 ms frame.
  CHECK(summary.one_percent_low_fps == 20.0);
}

void TestNearestRank() {
  std::vector<float> sorted = {1, 2, 3, 4};
  CHECK(Percentile(sorted, 0.5) == 2);
  CHECK(Percentile(sorted, 0.95) == 4);
  CHECK(Percentile(sorted, 0.0) == 1);
}

}  // namespace

int main() {
  TestEmpty();
  TestSteadyFrames();
  TestHitches();
  TestNearestRank();
  return check::Finish("debug_saulo_frame_stats_tests");
}
