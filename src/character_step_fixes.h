#pragma once

namespace crackdown {

// steps is the update's length in the title's original 1/30 s steps; the ledge pull must stay per that step.
constexpr float ScaleLedgeSnapDown(float height_pull, double steps) {
  return steps > 0.0 && steps != 1.0 ? height_pull * float(steps) : height_pull;
}

}  // namespace crackdown
