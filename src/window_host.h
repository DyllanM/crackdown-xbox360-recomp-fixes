#pragma once

#include <functional>

namespace rex::ui {
class Window;
class WindowedAppContext;
}  // namespace rex::ui

// F11 calls `toggle_window_mode`, which switches the window and records the choice. The cursor hides in focused
// fullscreen after a few seconds without motion. `mouse_free` says whether the game may own the mouse (no overlay is
// using it).
void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& context, std::function<bool()> mouse_free,
                      std::function<void()> toggle_window_mode);

// Must run before the window is destroyed.
void DetachWindowHost();
