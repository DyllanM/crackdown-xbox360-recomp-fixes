#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "window_placement.h"

struct ImGuiIO;

inline constexpr char kWindowTitle[] = "Crackdown Recompiled";

namespace crackdown {

// ImGui and the settings file take UTF-8 paths.
std::string Utf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(const std::string& utf8);

// The folder the player sees the program in: the executable's, or the AppImage's.
std::filesystem::path PortFolder();

// The next free <name>_NNN.log in `folder`, which is created. Empty when it cannot be created.
std::filesystem::path NextLogFile(const std::filesystem::path& folder, std::string_view name);

struct UiFontFiles {
  std::filesystem::path regular, semibold, bold;
};
UiFontFiles FindUiFonts();

// Names the application to the Linux desktop; SDL uses it as the Wayland app_id and the X11 WM_CLASS class, so it
// has to be set before the window is created. A no-op on Windows.
void SetApplicationId();

// Gives the window the application icon. Windows takes it from the executable's resources; on Linux SDL does not.
void ApplyWindowIcon();

// Shows a message that needs no GPU. Windows uses a message box; Linux draws on an SDL surface because SDL's own
// message box runs zenity first, which can stall before any window appears.
void ShowFatalMessage(const std::string& text);

// False when no window can be created because the session has no usable display; logs why and flushes the log.
bool GraphicalSessionAvailable();

struct GraphicsFallbackInfo {
  std::string game_dir;
  std::string game_files_status;
  bool game_files_ready = false;
};

// Tells the player that graphics could not start. Windows shows a message box. Linux opens the launcher's window,
// drawn on an SDL surface with no GPU: it shows the game files and graphics status, and Play explains the failure.
// Returns when the player closes it.
void RunLauncherWithoutGraphics(const GraphicsFallbackInfo& info);

// True while Shift is held. On Linux SDL only knows the keyboard once a window has focus.
bool ShiftHeld();

struct MonitorInfo {
  int left = 0, top = 0;                // physical pixels
  int width = 0, height = 0;
  int work_width = 0, work_height = 0;  // without the taskbar
  float scale = 1.0f;                   // display scaling
  bool primary = false;
};

// In the order ReXGlue's `monitor` setting numbers them: the primary display, then the system's order.
std::vector<MonitorInfo> ListMonitors();

// The display a `monitor` setting names. 0 (Default) and 1 are the primary display.
MonitorInfo MonitorForSetting(int monitor);

// Logs when the window did not open on the display the monitor setting names.
void CheckWindowDisplay(void* native_window, int monitor);

// Display work areas in screen pixels.
std::vector<Rect> WorkAreas();

// The native window's state, or nothing when the handle is not a window.
std::optional<WindowState> QueryWindowState(void* native_handle);

// Puts the window at the placement: the restored rectangle first, then maximized when asked.
void ApplyWindowPlacement(void* native_handle, const WindowPlacement& placement);

struct PreShowResult {
  WindowPlacement startup;  // where the window was created
  WindowPlacement applied;
};

// Applies the plan to the next top-level window of this thread before it is first shown. Windows only: elsewhere
// the launcher places the window once it exists.
void InstallPreShowPlacement(const PreShowPlan& plan, const WindowPlacement& saved);

// Set once the window has been shown; empty when no window was placed.
std::optional<PreShowResult> TakePreShowResult();

// Starts a new instance with this run's command line plus `extra_args`.
bool RelaunchSelf(std::wstring_view extra_args);

// Feeds an Xbox controller to Dear ImGui while the launcher is open. The runtime's own input system only starts
// with the game.
class LauncherGamepad {
 public:
  void Update(ImGuiIO& io);
  void Release(ImGuiIO& io);

 private:
  int pad_ = -1;
  double next_scan_ = 0.0;
#if !defined(_WIN32)
  bool subsystem_ = false;
#endif
};

}  // namespace crackdown
