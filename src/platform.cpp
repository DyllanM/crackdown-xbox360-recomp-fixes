#include "platform.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#include <shellscalingapi.h>
#include <xinput.h>
#else
#include <fstream>

#include <spawn.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#endif

#include <fmt/format.h>
#include <imgui.h>

#include <rex/filesystem.h>
#include <rex/logging.h>

#if defined(_WIN32)
// The NVIDIA Optimus and AMD switchable graphics drivers run programs exporting these on the discrete GPU.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace crackdown {
namespace {

#if defined(_WIN32)

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    return TRUE;
  }
  MonitorInfo found;
  found.left = info.rcMonitor.left;
  found.top = info.rcMonitor.top;
  found.width = info.rcMonitor.right - info.rcMonitor.left;
  found.height = info.rcMonitor.bottom - info.rcMonitor.top;
  found.work_width = info.rcWork.right - info.rcWork.left;
  found.work_height = info.rcWork.bottom - info.rcWork.top;
  found.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
  UINT dpi_x = 0;
  UINT dpi_y = 0;
  if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) && dpi_x) {
    found.scale = float(dpi_x) / 96.0f;
  }
  reinterpret_cast<std::vector<MonitorInfo>*>(user)->push_back(found);
  return TRUE;
}

BOOL CALLBACK AddWorkArea(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info)) {
    reinterpret_cast<std::vector<Rect>*>(user)->push_back(
        {info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom});
  }
  return TRUE;
}

PreShowPlan g_plan;
WindowPlacement g_saved;
HHOOK g_hook = nullptr;
std::optional<PreShowResult> g_result;

LRESULT CALLBACK PreShowProc(int code, WPARAM wparam, LPARAM lparam) {
  const auto* message = reinterpret_cast<const CWPSTRUCT*>(lparam);
  if (code == HC_ACTION && message->message == WM_SHOWWINDOW && message->wParam && message->lParam == 0 &&
      !GetParent(message->hwnd) && !g_result) {
    HHOOK hook = g_hook;
    g_hook = nullptr;
    const HWND window = message->hwnd;
    RECT before;
    GetWindowRect(window, &before);
    g_result = PreShowResult{{{before.left, before.top, before.right, before.bottom}, false}, g_saved};
    if (g_plan.normal) {
      SetWindowPos(window, nullptr, g_plan.normal->left, g_plan.normal->top, g_plan.normal->width(),
                   g_plan.normal->height(), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (g_plan.maximized) {
      ShowWindow(window, SW_MAXIMIZE);
    }
    UnhookWindowsHookEx(hook);
    return 0;
  }
  return CallNextHookEx(g_hook, code, wparam, lparam);
}

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetStateFn LoadXInput() {
  for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
    if (HMODULE dll = LoadLibraryW(name)) {
      if (FARPROC proc = GetProcAddress(dll, "XInputGetState")) {
        return reinterpret_cast<XInputGetStateFn>(reinterpret_cast<void*>(proc));
      }
    }
  }
  return nullptr;
}

DWORD GetPadState(DWORD pad, XINPUT_STATE* state) {
  static const XInputGetStateFn get_state = LoadXInput();
  return get_state ? get_state(pad, state) : ERROR_DEVICE_NOT_CONNECTED;
}

struct ButtonKey {
  ImGuiKey key;
  WORD mask;
};
constexpr ButtonKey kButtonKeys[] = {
    {ImGuiKey_GamepadStart, XINPUT_GAMEPAD_START},
    {ImGuiKey_GamepadBack, XINPUT_GAMEPAD_BACK},
    {ImGuiKey_GamepadFaceDown, XINPUT_GAMEPAD_A},
    {ImGuiKey_GamepadFaceRight, XINPUT_GAMEPAD_B},
    {ImGuiKey_GamepadFaceLeft, XINPUT_GAMEPAD_X},
    {ImGuiKey_GamepadFaceUp, XINPUT_GAMEPAD_Y},
    {ImGuiKey_GamepadDpadLeft, XINPUT_GAMEPAD_DPAD_LEFT},
    {ImGuiKey_GamepadDpadRight, XINPUT_GAMEPAD_DPAD_RIGHT},
    {ImGuiKey_GamepadDpadUp, XINPUT_GAMEPAD_DPAD_UP},
    {ImGuiKey_GamepadDpadDown, XINPUT_GAMEPAD_DPAD_DOWN},
};

struct StickKey {
  ImGuiKey key;
  bool horizontal;
  int dead_zone;
  int full;
};
constexpr StickKey kStickKeys[] = {
    {ImGuiKey_GamepadLStickLeft, true, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768},
    {ImGuiKey_GamepadLStickRight, true, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 32767},
    {ImGuiKey_GamepadLStickUp, false, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 32767},
    {ImGuiKey_GamepadLStickDown, false, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768},
};

#else

constexpr char kApplicationId[] = "io.github.saulob.crackdown";
constexpr char kIconName[] = "crackdown.png";

struct ButtonKey {
  ImGuiKey key;
  SDL_GamepadButton button;
};
constexpr ButtonKey kButtonKeys[] = {
    {ImGuiKey_GamepadStart, SDL_GAMEPAD_BUTTON_START},
    {ImGuiKey_GamepadBack, SDL_GAMEPAD_BUTTON_BACK},
    {ImGuiKey_GamepadFaceDown, SDL_GAMEPAD_BUTTON_SOUTH},
    {ImGuiKey_GamepadFaceRight, SDL_GAMEPAD_BUTTON_EAST},
    {ImGuiKey_GamepadFaceLeft, SDL_GAMEPAD_BUTTON_WEST},
    {ImGuiKey_GamepadFaceUp, SDL_GAMEPAD_BUTTON_NORTH},
    {ImGuiKey_GamepadDpadLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {ImGuiKey_GamepadDpadRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {ImGuiKey_GamepadDpadUp, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {ImGuiKey_GamepadDpadDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
};

// The dead zone XInput uses for the left stick. SDL's Y axis points down.
constexpr int kStickDeadZone = 7849;
struct StickKey {
  ImGuiKey key;
  SDL_GamepadAxis axis;
  int direction;
};
constexpr StickKey kStickKeys[] = {
    {ImGuiKey_GamepadLStickLeft, SDL_GAMEPAD_AXIS_LEFTX, -1},
    {ImGuiKey_GamepadLStickRight, SDL_GAMEPAD_AXIS_LEFTX, 1},
    {ImGuiKey_GamepadLStickUp, SDL_GAMEPAD_AXIS_LEFTY, -1},
    {ImGuiKey_GamepadLStickDown, SDL_GAMEPAD_AXIS_LEFTY, 1},
};

SDL_Gamepad* OpenFirstGamepad() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  SDL_Gamepad* pad = ids && count > 0 ? SDL_OpenGamepad(ids[0]) : nullptr;
  SDL_free(ids);
  return pad;
}

struct FontFamily {
  const char* regular;
  const char* semibold;
  const char* bold;
};
// The first family whose regular face exists wins.
constexpr FontFamily kFontFamilies[] = {
    {"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-SemiBold.ttf",
     "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"},
    {"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-SemiBold.ttf",
     "/usr/share/fonts/noto/NotoSans-Bold.ttf"},
    {"/usr/share/fonts/google-noto/NotoSans-Regular.ttf", "/usr/share/fonts/google-noto/NotoSans-SemiBold.ttf",
     "/usr/share/fonts/google-noto/NotoSans-Bold.ttf"},
    {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", nullptr, "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"},
    {"/usr/share/fonts/TTF/DejaVuSans.ttf", nullptr, "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"},
    {"/usr/share/fonts/dejavu/DejaVuSans.ttf", nullptr, "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"},
    {"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", nullptr,
     "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"},
    {"/usr/share/fonts/liberation/LiberationSans-Regular.ttf", nullptr,
     "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"},
};

// ReXGlue's native window handle is empty here, and the launcher is its only window.
SDL_Window* LauncherWindow() {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  SDL_Window* window = windows && count > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  return window;
}

std::filesystem::path IconFile() {
  return rex::filesystem::GetExecutableFolder() / ".." / "share" / "icons" / "hicolor" / "256x256" / "apps" /
         kIconName;
}

#endif

}  // namespace

std::string Utf8(const std::filesystem::path& path) {
  const std::u8string s = path.u8string();
  return std::string(s.begin(), s.end());
}

std::filesystem::path PathFromUtf8(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::filesystem::path PortFolder() {
#if !defined(_WIN32)
  // An AppImage runs from a read-only mount that is gone once it exits.
  if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage) {
    return std::filesystem::path(appimage).parent_path();
  }
#endif
  return rex::filesystem::GetExecutableFolder();
}

std::filesystem::path NextLogFile(const std::filesystem::path& folder, std::string_view name) {
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  if (!std::filesystem::is_directory(folder, ec)) {
    return {};
  }
  const std::string prefix = std::string(name) + "_";
  int last = 0;
  for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
    const std::string file = entry.path().filename().string();
    if (file.starts_with(prefix) && file.ends_with(".log")) {
      last = std::max(last, std::atoi(file.c_str() + prefix.size()));
    }
  }
  return folder / fmt::format("{}{:03d}.log", prefix, last + 1);
}

UiFontFiles FindUiFonts() {
#if defined(_WIN32)
  wchar_t windows[MAX_PATH];
  const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
  const std::filesystem::path fonts =
      std::filesystem::path(length ? std::wstring(windows, length) : std::wstring(L"C:\\Windows")) / L"Fonts";
  return {fonts / "segoeui.ttf", fonts / "seguisb.ttf", fonts / "segoeuib.ttf"};
#else
  for (const FontFamily& family : kFontFamilies) {
    std::error_code ec;
    if (std::filesystem::exists(family.regular, ec)) {
      return {family.regular, family.semibold ? family.semibold : family.bold, family.bold};
    }
  }
  return {};
#endif
}

bool ShiftHeld() {
#if defined(_WIN32)
  return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#else
  SDL_PumpEvents();
  return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
#endif
}

#if defined(_WIN32)

// SDL builds its display list the same way: the primary display, then EnumDisplayMonitors order.
std::vector<MonitorInfo> ListMonitors() {
  std::vector<MonitorInfo> monitors;
  EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&monitors));
  std::stable_partition(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; });
  return monitors;
}

#else

// Sizes are SDL window coordinates, the units ReXGlue sizes its window in (logical size times the scale).
std::vector<MonitorInfo> ListMonitors() {
  std::vector<MonitorInfo> monitors;
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
  for (int i = 0; displays && i < count; ++i) {
    MonitorInfo found;
    SDL_Rect bounds{};
    if (SDL_GetDisplayBounds(displays[i], &bounds)) {
      found.left = bounds.x;
      found.top = bounds.y;
      found.width = found.work_width = bounds.w;
      found.height = found.work_height = bounds.h;
    }
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(displays[i], &usable) && usable.w > 0 && usable.h > 0) {
      found.work_width = usable.w;
      found.work_height = usable.h;
    }
    if (const float scale = SDL_GetDisplayContentScale(displays[i]); scale > 0.0f) {
      found.scale = scale;
    }
    found.primary = displays[i] == primary;
    monitors.push_back(found);
  }
  SDL_free(displays);
  return monitors;
}

#endif

MonitorInfo MonitorForSetting(int monitor) {
  const std::vector<MonitorInfo> monitors = ListMonitors();
  if (monitors.empty()) {
    return {};
  }
  if (monitor >= 1 && monitor <= int(monitors.size())) {
    return monitors[size_t(monitor - 1)];
  }
  const auto primary = std::find_if(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; });
  return primary != monitors.end() ? *primary : monitors.front();
}

#if defined(_WIN32)

void ApplyWindowIcon() {}

void SetApplicationId() {}

void ShowFatalMessage(const std::string& text) {
  MessageBoxA(nullptr, text.c_str(), kWindowTitle, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

std::vector<Rect> WorkAreas() {
  std::vector<Rect> areas;
  EnumDisplayMonitors(nullptr, nullptr, AddWorkArea, reinterpret_cast<LPARAM>(&areas));
  return areas;
}

std::optional<WindowState> QueryWindowState(void* native_handle) {
  const HWND window = static_cast<HWND>(native_handle);
  RECT rect;
  if (!window || !IsWindow(window) || !GetWindowRect(window, &rect)) {
    return std::nullopt;
  }
  WindowState state;
  state.rect = {rect.left, rect.top, rect.right, rect.bottom};
  state.minimized = IsIconic(window) != FALSE;
  state.maximized = IsZoomed(window) != FALSE;
  if (state.minimized) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    state.maximized = GetWindowPlacement(window, &placement) && (placement.flags & WPF_RESTORETOMAXIMIZED);
  }
  return state;
}

void ApplyWindowPlacement(void* native_handle, const WindowPlacement& placement) {
  const HWND window = static_cast<HWND>(native_handle);
  if (!window || !IsWindow(window)) {
    return;
  }
  ShowWindow(window, SW_RESTORE);
  SetWindowPos(window, nullptr, placement.normal.left, placement.normal.top, placement.normal.width(),
               placement.normal.height(), SWP_NOZORDER | SWP_NOACTIVATE);
  if (placement.maximized) {
    ShowWindow(window, SW_MAXIMIZE);
  }
}

void InstallPreShowPlacement(const PreShowPlan& plan, const WindowPlacement& saved) {
  if (g_hook || !plan.Needed()) {
    return;
  }
  g_plan = plan;
  g_saved = saved;
  g_result.reset();
  g_hook = SetWindowsHookExW(WH_CALLWNDPROC, PreShowProc, nullptr, GetCurrentThreadId());
}

std::optional<PreShowResult> TakePreShowResult() {
  if (g_hook) {
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
  }
  std::optional<PreShowResult> result = g_result;
  g_result.reset();
  return result;
}

void CheckWindowDisplay(void* native_window, int monitor) {
  const HWND hwnd = static_cast<HWND>(native_window);
  const MonitorInfo expected = MonitorForSetting(monitor);
  if (!hwnd || expected.width == 0) {
    return;
  }
  MONITORINFO actual{sizeof(actual)};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &actual)) {
    return;
  }
  if (actual.rcMonitor.left != expected.left || actual.rcMonitor.top != expected.top) {
    REXLOG_WARN("Window opened on the display at ({}, {}), but monitor setting {} names ({}, {})",
                actual.rcMonitor.left, actual.rcMonitor.top, monitor, expected.left, expected.top);
  }
}

bool RelaunchSelf(std::wstring_view extra_args) {
  std::wstring command_line = GetCommandLineW();
  command_line += L' ';
  command_line += extra_args;
  wchar_t exe[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
    return false;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                      &process)) {
    REXLOG_ERROR("Relaunch failed ({})", GetLastError());
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

void LauncherGamepad::Update(ImGuiIO& io) {
  XINPUT_STATE state{};
  if (pad_ < 0 || GetPadState(DWORD(pad_), &state) != ERROR_SUCCESS) {
    pad_ = -1;
    // Polling an empty slot is slow, so look for a controller once a second.
    if (ImGui::GetTime() < next_scan_) {
      return;
    }
    next_scan_ = ImGui::GetTime() + 1.0;
    for (DWORD i = 0; i < XUSER_MAX_COUNT && pad_ < 0; ++i) {
      if (GetPadState(i, &state) == ERROR_SUCCESS) {
        pad_ = int(i);
      }
    }
    if (pad_ < 0) {
      return;
    }
  }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  const XINPUT_GAMEPAD& pad = state.Gamepad;
  for (const ButtonKey& b : kButtonKeys) {
    io.AddKeyEvent(b.key, (pad.wButtons & b.mask) != 0);
  }
  for (const StickKey& s : kStickKeys) {
    const int value = s.horizontal ? pad.sThumbLX : pad.sThumbLY;
    const float amount = std::clamp(float(value - s.dead_zone) / float(s.full - s.dead_zone), 0.0f, 1.0f);
    io.AddKeyAnalogEvent(s.key, amount > 0.1f, amount);
  }
}

void LauncherGamepad::Release(ImGuiIO& io) {
  for (const ButtonKey& b : kButtonKeys) {
    io.AddKeyEvent(b.key, false);
  }
  for (const StickKey& s : kStickKeys) {
    io.AddKeyAnalogEvent(s.key, false, 0.0f);
  }
  pad_ = -1;
}

#else

void SetApplicationId() {
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING, kApplicationId);
}

namespace {

struct Color {
  int r, g, b;
};

constexpr Color kPanel{0x2b, 0x2b, 0x2b};
constexpr Color kText{0xf0, 0xf0, 0xf0};
constexpr int kMessageWidth = 560;
constexpr int kMessageMargin = 16;
constexpr int kMessageLineStep = 20;
constexpr int kMessageColumns = 64;

class SoftwareText {
 public:
  SoftwareText() {
    ImFont* font = atlas_.AddFontDefault();
    baked_ = font->GetFontBaked(font->LegacySize);
    // Loading a glyph rewrites the atlas pixels, so every glyph is loaded before they are read.
    for (ImWchar c = kFirstGlyph; c <= kLastGlyph; ++c) {
      baked_->FindGlyph(c);
    }
    atlas_.GetTexDataAsAlpha8(&pixels_, &width_, &height_);
  }

  void Draw(SDL_Surface* surface, std::string_view text, int x, int y, Color color) const {
    float pen_x = float(x);
    for (const char c : text) {
      const ImFontGlyph* glyph = baked_->FindGlyph(Printable(c));
      DrawGlyph(surface, *glyph, int(pen_x), y, color);
      pen_x += glyph->AdvanceX;
    }
  }

 private:
  static constexpr ImWchar kFirstGlyph = 0x20;
  static constexpr ImWchar kLastGlyph = 0x7e;

  static ImWchar Printable(char c) {
    const auto code = static_cast<unsigned char>(c);
    return code >= kFirstGlyph && code <= kLastGlyph ? ImWchar(code) : ImWchar('?');
  }

  void DrawGlyph(SDL_Surface* surface, const ImFontGlyph& glyph, int x, int y, Color color) const {
    if (!glyph.Visible) {
      return;
    }
    const int source_x = int(glyph.U0 * float(width_) + 0.5f);
    const int source_y = int(glyph.V0 * float(height_) + 0.5f);
    const int width = int(glyph.X1 - glyph.X0 + 0.5f);
    const int height = int(glyph.Y1 - glyph.Y0 + 0.5f);
    for (int row = 0; row < height; ++row) {
      for (int column = 0; column < width; ++column) {
        const int alpha = pixels_[(source_y + row) * width_ + source_x + column];
        if (alpha != 0) {
          Blend(surface, x + int(glyph.X0 + 0.5f) + column, y + int(glyph.Y0 + 0.5f) + row, color, alpha);
        }
      }
    }
  }

  static void Blend(SDL_Surface* surface, int x, int y, Color color, int alpha) {
    Uint8 r, g, b, a;
    if (x < 0 || y < 0 || x >= surface->w || y >= surface->h ||
        !SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a)) {
      return;
    }
    SDL_WriteSurfacePixel(surface, x, y, Uint8(r + (color.r - r) * alpha / 255),
                          Uint8(g + (color.g - g) * alpha / 255), Uint8(b + (color.b - b) * alpha / 255), 0xff);
  }

  ImFontAtlas atlas_;
  ImFontBaked* baked_ = nullptr;
  unsigned char* pixels_ = nullptr;
  int width_ = 0;
  int height_ = 0;
};

std::vector<std::string> WrapLines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream paragraphs(text);
  for (std::string paragraph; std::getline(paragraphs, paragraph);) {
    std::istringstream words(paragraph);
    std::string line;
    for (std::string word; words >> word;) {
      if (!line.empty() && line.size() + 1 + word.size() > size_t(kMessageColumns)) {
        lines.push_back(line);
        line.clear();
      }
      line += (line.empty() ? "" : " ") + word;
    }
    lines.push_back(line);
  }
  return lines;
}

void DrawMessage(SDL_Window* window, const SoftwareText& text, const std::vector<std::string>& lines) {
  SDL_Surface* surface = SDL_GetWindowSurface(window);
  if (!surface) {
    return;
  }
  SDL_Rect all{0, 0, surface->w, surface->h};
  SDL_FillSurfaceRect(surface, &all, SDL_MapSurfaceRGB(surface, Uint8(kPanel.r), Uint8(kPanel.g), Uint8(kPanel.b)));
  int y = kMessageMargin;
  for (const std::string& line : lines) {
    text.Draw(surface, line, kMessageMargin, y, kText);
    y += kMessageLineStep;
  }
  SDL_UpdateWindowSurface(window);
}

}  // namespace

void ShowFatalMessage(const std::string& message) {
  std::vector<std::string> lines = WrapLines(message);
  lines.emplace_back();
  lines.emplace_back("Press a key or click to close.");
  const SoftwareText text;
  const int height = kMessageMargin * 2 + kMessageLineStep * int(lines.size());
  SDL_Window* window = SDL_CreateWindow(kWindowTitle, kMessageWidth, height, 0);
  if (!window || !SDL_GetWindowSurface(window)) {
    REXLOG_ERROR("Could not show the message window: {}", SDL_GetError());
    SDL_DestroyWindow(window);
    return;
  }
  DrawMessage(window, text, lines);
  for (bool open = true; open;) {
    SDL_Event event;
    if (!SDL_WaitEvent(&event)) {
      break;
    }
    switch (event.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        open = false;
        break;
      case SDL_EVENT_WINDOW_EXPOSED:
        DrawMessage(window, text, lines);
        break;
      default:
        break;
    }
  }
  SDL_DestroyWindow(window);
}

void ApplyWindowIcon() {
  SDL_Window* window = LauncherWindow();
  if (!window) {
    return;
  }
  if (SDL_Surface* icon = SDL_LoadPNG(Utf8(IconFile()).c_str())) {
    SDL_SetWindowIcon(window, icon);
    SDL_DestroySurface(icon);
  }
}

std::vector<Rect> WorkAreas() {
  std::vector<Rect> areas;
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  for (int i = 0; displays && i < count; ++i) {
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(displays[i], &usable) && usable.w > 0 && usable.h > 0) {
      areas.push_back({usable.x, usable.y, usable.x + usable.w, usable.y + usable.h});
    }
  }
  SDL_free(displays);
  return areas;
}

std::optional<WindowState> QueryWindowState(void*) {
  SDL_Window* window = LauncherWindow();
  if (!window) {
    return std::nullopt;
  }
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  SDL_GetWindowPosition(window, &x, &y);
  SDL_GetWindowSize(window, &width, &height);
  const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
  WindowState state;
  state.rect = {x, y, x + width, y + height};
  state.minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
  state.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
  return state;
}

void ApplyWindowPlacement(void*, const WindowPlacement& placement) {
  SDL_Window* window = LauncherWindow();
  if (!window) {
    return;
  }
  SDL_RestoreWindow(window);
  SDL_SyncWindow(window);
  SDL_SetWindowSize(window, placement.normal.width(), placement.normal.height());
  SDL_SetWindowPosition(window, placement.normal.left, placement.normal.top);
  if (placement.maximized) {
    SDL_MaximizeWindow(window);
  }
}

void InstallPreShowPlacement(const PreShowPlan&, const WindowPlacement&) {}

std::optional<PreShowResult> TakePreShowResult() {
  return std::nullopt;
}

// ListMonitors is SDL's own display order here, so the monitor setting always names the right display.
void CheckWindowDisplay(void*, int) {}

bool RelaunchSelf(std::wstring_view extra_args) {
  std::vector<std::string> args;
  std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
  for (std::string arg; std::getline(cmdline, arg, '\0');) {
    args.push_back(arg);
  }
  std::string extra;
  for (const wchar_t c : extra_args) {
    extra += char(c);  // ASCII flags
  }
  args.push_back(extra);
  // Inside an AppImage, start the AppImage again: this process's mount closes when it exits.
  std::error_code ec;
  const char* appimage = std::getenv("APPIMAGE");
  const std::string exe =
      appimage && *appimage ? std::string(appimage) : std::filesystem::read_symlink("/proc/self/exe", ec).string();
  if (exe.empty() || args.size() < 2) {
    REXLOG_ERROR("Relaunch failed: cannot read this process's command line");
    return false;
  }
  args.front() = exe;
  std::vector<char*> argv;
  for (std::string& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  pid_t pid = 0;
  if (const int error = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), environ); error != 0) {
    REXLOG_ERROR("Relaunch failed ({})", error);
    return false;
  }
  return true;
}

void LauncherGamepad::Update(ImGuiIO& io) {
  SDL_Gamepad* pad = pad_ >= 0 ? SDL_GetGamepadFromID(SDL_JoystickID(pad_)) : nullptr;
  if (!pad || !SDL_GamepadConnected(pad)) {
    if (pad) {
      SDL_CloseGamepad(pad);
    }
    pad_ = -1;
    if (ImGui::GetTime() < next_scan_) {
      return;
    }
    next_scan_ = ImGui::GetTime() + 1.0;
    // Started here and quit in Release; SDL counts the starts, so the game's own input is unaffected.
    if (!subsystem_ && !(subsystem_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD))) {
      return;
    }
    pad = OpenFirstGamepad();
    if (!pad) {
      return;
    }
    pad_ = int(SDL_GetGamepadID(pad));
  }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  for (const ButtonKey& b : kButtonKeys) {
    io.AddKeyEvent(b.key, SDL_GetGamepadButton(pad, b.button));
  }
  for (const StickKey& s : kStickKeys) {
    const int value = s.direction * int(SDL_GetGamepadAxis(pad, s.axis));
    const float amount = std::clamp(float(value - kStickDeadZone) / float(32767 - kStickDeadZone), 0.0f, 1.0f);
    io.AddKeyAnalogEvent(s.key, amount > 0.1f, amount);
  }
}

void LauncherGamepad::Release(ImGuiIO& io) {
  for (const ButtonKey& b : kButtonKeys) {
    io.AddKeyEvent(b.key, false);
  }
  for (const StickKey& s : kStickKeys) {
    io.AddKeyAnalogEvent(s.key, false, 0.0f);
  }
  if (pad_ >= 0) {
    if (SDL_Gamepad* pad = SDL_GetGamepadFromID(SDL_JoystickID(pad_))) {
      SDL_CloseGamepad(pad);
    }
  }
  pad_ = -1;
  if (subsystem_) {
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    subsystem_ = false;
  }
}

#endif

}  // namespace crackdown
