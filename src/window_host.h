#pragma once

#include <functional>

namespace rex::ui {
class ImGuiDrawer;
class Window;
class WindowedAppContext;
}  // namespace rex::ui

// The cursor follows the last gameplay input device: mouse shows it, keyboard or controller hides it. Overlays
// and a lost focus force it visible, and mouse look keeps its own capture. `ui_wants_mouse` reports whether ImGui
// currently captures the mouse.
void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& context,
                      std::function<bool()> ui_wants_mouse, std::function<void()> toggle_window_mode);

// Must run before the window is destroyed.
void DetachWindowHost();

// Called while an in-game overlay owns input.
void SetCursorOverlayOpen(bool open);

// Keeps the cursor in step with ImGui capture that does not come from an overlay.
void CreateCursorSync(rex::ui::ImGuiDrawer* drawer);
