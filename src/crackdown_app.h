#pragma once

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/windowed_app_context.h>

#include "cache_device.h"
#include "debug_saulo_diagnostics.h"
#include "frame_counter.h"
#include "frame_rate.h"
#include "game_files.h"
#include "launcher.h"
#include "platform.h"
#include "runtime_health.h"
#include "settings.h"
#include "skip_intro.h"
#include "user_data.h"
#include "window_host.h"

class CrackdownApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    crackdown::SetApplicationId();
    return std::unique_ptr<CrackdownApp>(new CrackdownApp(ctx, "crackdown", PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    shipped_config_ = paths.config_path;
    const std::filesystem::path port_folder = crackdown::PortFolder();
    if (paths.game_data_root.empty()) {
      paths.game_data_root = port_folder / "game";
    }
    ChooseUserData(paths, port_folder);
    // ReXGlue writes its logs beside the executable, which is read-only inside an AppImage.
    if (port_folder != rex::filesystem::GetExecutableFolder() && rex::cvar::GetFlagByName("log_file").empty()) {
      for (const auto& logs : {port_folder / "logs", paths.user_data_root / "logs"}) {
        if (const std::filesystem::path file = crackdown::NextLogFile(logs, "crackdown"); !file.empty()) {
          rex::cvar::SetFlagByName("log_file", crackdown::Utf8(file));
          break;
        }
      }
    }
  }

  bool SetupPresentation() override {
    if (rex::ReXApp::SetupPresentation()) {
      return true;
    }
    crackdown::ShowFatalMessage(
        "The graphics system could not start. Crackdown needs a graphics driver with Direct3D 12 (Windows) or "
        "Vulkan (Linux). Update the driver and start the game again. The log folder next to the program has the "
        "details.");
    return false;
  }

  void OnPostInitLogging() override {
    StartRuntimeHealth();
    const crackdown::UserDataMessage message = crackdown::DescribeUserData(user_data_);
    if (message.warning) {
      REXLOG_WARN("{}", message.text);
    } else {
      REXLOG_INFO("{}", message.text);
    }
    crackdown::LoadPlayerSettings(crackdown::PlayerSettingsPath(user_data_root()));
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    crackdown::LoadLauncherFonts(atlas, window() ? float(window()->GetDpi()) / float(window()->GetMediumDpi()) : 1.0f);
  }

  void OnPreSetup(rex::RuntimeConfig&) override {
    game_settings_.Capture(crackdown::PlayerSettingsPath(user_data_root()), shipped_config_);
    show_launcher_ = crackdown::ShouldShowLauncher(game_data_root());
    if (show_launcher_) {
      crackdown::PrepareLauncherWindow();
    }
    game_settings_.MarkWindowCreated();
    crackdown::FitWindowToDisplay();
  }

  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (window()) {
      window()->SetTitle(kWindowTitle);
    }
    crackdown::ApplyWindowIcon();
    game_settings_.CaptureLateRegistered();
    if (!show_launcher_ && crackdown::ShiftHeldAfterWindowOpened()) {
      show_launcher_ = true;
    }
    if (!show_launcher_) {
      crackdown::ApplyMultisampling();
      return defaults;
    }
    if (!imgui_drawer()) {
      game_settings_.Apply();
      return defaults;
    }
    REXLOG_INFO("Launcher shown");
    crackdown::LauncherCallbacks callbacks;
    callbacks.play = [this, resume, defaults] {
      app_context().CallInUIThreadDeferred([this, resume, defaults] {
        game_settings_.Apply();
        game_settings_.ApplyDisplayMode();
        resume(defaults);
      });
    };
    callbacks.window_changed = [this] { return game_settings_.WindowChanged(); };
    callbacks.restart = [this] {
      if (!crackdown::RelaunchSelf(L"--crackdown_skip_launcher=true")) {
        return false;
      }
      app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); });
      return true;
    };
    callbacks.quit = [this] { app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); }); };
    callbacks.window_handle = [this] { return window() ? window()->GetNativeWindowHandle() : nullptr; };
    callbacks.window_fullscreen = [this] { return window() && window()->IsFullscreen(); };
    callbacks.set_window_fullscreen = [this](bool fullscreen) {
      if (window()) {
        window()->SetFullscreen(fullscreen);
      }
    };
    crackdown::ShowLauncher(imgui_drawer(),
                            {defaults.game_data_root, defaults.user_data_root,
                             crackdown::PlayerSettingsPath(defaults.user_data_root), user_data_.outcome},
                            game_settings_, std::move(callbacks));
    return std::nullopt;
  }

  void OnPostSetup() override {
    if (runtime()) {
      MountCacheDevice(runtime()->file_system(), user_data_root());
    }
    if (window()) {
      AttachWindowHost(
          window(), app_context(),
          [this] { return !imgui_drawer() || !imgui_drawer()->GetIO().WantCaptureMouse; },
          [this] { ToggleWindowMode(); });
      crackdown::AttachFrameRate(window(), app_context());
      crackdown::CheckWindowDisplay(window()->GetNativeWindowHandle(), REXCVAR_GET(monitor));
    }
    if (imgui_drawer()) {
      crackdown::CreateFrameCounter(imgui_drawer());
    }
    InstallDebugSauloFaultProbe();
    InstallSkipIntro();
  }

  void OnFileDrop(rex::ui::FileDropEvent& e) override { crackdown::LauncherFileDropped(e.filename()); }

  bool OnWindowCloseRequested() override {
    StopRuntimeHealth();
    return true;
  }
  void OnGuestThreadExit(rex::system::XThread*) override { StopRuntimeHealth(); }

  void OnShutdown() override {
    // ReXApp destroys the window before the runtime; the input drivers must detach while it still exists.
    if (auto* input = runtime() ? runtime()->input_system() : nullptr) {
      input->Shutdown();
    }
    DetachWindowHost();
    crackdown::DetachFrameRate();
    StopRuntimeHealth();
  }

 private:
  // F11 changes the window at once and keeps the choice as the player's Display mode for the next start.
  void ToggleWindowMode() {
    const bool fullscreen = !window()->IsFullscreen();
    const std::string value = fullscreen ? "true" : "false";
    window()->SetFullscreen(fullscreen);
    rex::cvar::SetFlagByName(crackdown::kFullscreenKey, value);
    game_settings_.SaveNow(crackdown::kFullscreenKey, value);
  }

  // The cache folder ReXGlue derived from the system root follows the user data root unless it was set explicitly.
  void ChooseUserData(rex::PathConfig& paths, const std::filesystem::path& port_folder) {
    user_data_ = crackdown::SelectUserData(
        {port_folder, REXCVAR_GET(user_data_root).empty() ? std::filesystem::path() : paths.user_data_root});
    if (user_data_.outcome == crackdown::UserDataOutcome::kNotWritable) {
      crackdown::ShowFatalMessage(crackdown::UserDataNotWritableText(user_data_));
      std::_Exit(1);
    }
    if (user_data_.root == paths.user_data_root) {
      return;
    }
    paths.user_data_root = user_data_.root;
    if (REXCVAR_GET(cache_root).empty()) {
      paths.cache_root = user_data_.root / "cache";
    }
  }

  crackdown::GameSettings game_settings_;
  std::filesystem::path shipped_config_;
  crackdown::UserDataSelection user_data_;
  bool show_launcher_ = false;
};
