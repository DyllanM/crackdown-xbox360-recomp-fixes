#pragma once

#include <cstdint>

namespace rex::ui {
class Window;
class WindowedAppContext;
}  // namespace rex::ui

namespace crackdown {

// The latest one-second sample of the game's swaps; all zero until the first one is measured.
struct GuestFrameStats {
  double fps = 0;
  double frame_time_ms = 0;
  uint64_t frame_count = 0;
};

// Updates the window title with the sampled rate while crackdown_show_fps is on. Call once the window exists.
void AttachFrameRate(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context);
void DetachFrameRate();

GuestFrameStats GetGuestFrameStats();

}  // namespace crackdown
