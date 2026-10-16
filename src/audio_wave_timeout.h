#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "frame_timing.h"

namespace crackdown {

// Fractional 30 Hz ticks of each wave instance's safety timeout. The title adds one per update, which at a higher
// frame rate stops speech early.
class WaveTimeoutClock {
 public:
  // Whole ticks for one update of `original_steps` title steps; the rest stays with the instance.
  uint32_t Tick(uint32_t instance, double original_steps) {
    std::lock_guard lock(mutex_);
    double& carry = carry_[instance];
    carry += original_steps;
    return TakeWhole(carry);
  }

  // Pooled instances are reused, and a new clip must not inherit the previous one's carry.
  void Reset(uint32_t instance) {
    std::lock_guard lock(mutex_);
    carry_.erase(instance);
  }

 private:
  std::mutex mutex_;
  std::unordered_map<uint32_t, double> carry_;
};

}  // namespace crackdown
