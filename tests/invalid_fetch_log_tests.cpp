#include <cstdint>

#include <rex/graphics/pipeline/texture/invalid_fetch_log.h>

#include "check.h"

using namespace rex::graphics;

namespace {

using Signature = InvalidFetchLogTracker::Signature;

constexpr Signature kCrackdownFetch = {0x00000000, 0x00000000, 0x00000000,
                                       0x01280000, 0x00000003, 0x00000000};

Signature Unique(uint32_t index) {
  return {0xFFFF0000u + index, 0, 0, 0, 0, 0};
}

void TestFirstAndRepeated() {
  InvalidFetchLogTracker tracker;
  CHECK(tracker.Check(kCrackdownFetch) == InvalidFetchLogDecision::kLog);
  CHECK(tracker.Check(kCrackdownFetch) == InvalidFetchLogDecision::kSuppress);
  for (int i = 0; i < 1000; ++i) {
    CHECK(tracker.Check(kCrackdownFetch) == InvalidFetchLogDecision::kSuppress);
  }
  CHECK(tracker.count() == 1);
}

void TestDifferentSignature() {
  InvalidFetchLogTracker tracker;
  CHECK(tracker.Check(kCrackdownFetch) == InvalidFetchLogDecision::kLog);
  Signature other = kCrackdownFetch;
  other[5] = 1;
  CHECK(tracker.Check(other) == InvalidFetchLogDecision::kLog);
  CHECK(tracker.Check(other) == InvalidFetchLogDecision::kSuppress);
  CHECK(tracker.count() == 2);
}

void TestCapacityAndOverflow() {
  InvalidFetchLogTracker tracker;
  for (uint32_t i = 0; i < InvalidFetchLogTracker::kMaxSignatures; ++i) {
    CHECK(tracker.Check(Unique(i)) == InvalidFetchLogDecision::kLog);
  }
  CHECK(tracker.count() == 16);
  CHECK(tracker.Check(Unique(100)) == InvalidFetchLogDecision::kLogOverflow);
  for (uint32_t i = 101; i < 200; ++i) {
    CHECK(tracker.Check(Unique(i)) == InvalidFetchLogDecision::kSuppress);
  }
  CHECK(tracker.Check(Unique(100)) == InvalidFetchLogDecision::kSuppress);
  CHECK(tracker.Check(Unique(0)) == InvalidFetchLogDecision::kSuppress);
  CHECK(tracker.count() == 16);
}

}  // namespace

int main() {
  TestFirstAndRepeated();
  TestDifferentSignature();
  TestCapacityAndOverflow();
  return check::Finish("invalid_fetch_log_tests");
}
