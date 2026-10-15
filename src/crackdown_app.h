#pragma once

#include <rex/rex_app.h>

#include "cache_device.h"
#include "debug_saulo_diagnostics.h"
#include "debug_saulo_no_intro.h"  // DEBUG_SAULO
#include "fullscreen_toggle.h"
#include "runtime_health.h"

class CrackdownApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<CrackdownApp>(new CrackdownApp(ctx, "crackdown",
        PPCImageConfig));
  }

  void OnPostInitLogging() override { StartRuntimeHealth(); }
  void OnPostSetup() override {
    if (runtime()) {
      MountCacheDevice(runtime()->file_system(), user_data_root());
    }
    if (window()) {
      AttachFullscreenToggle(window());
    }
    InstallDebugSauloFaultProbe();
    InstallDebugSauloNoIntro();  // DEBUG_SAULO
  }
  bool OnWindowCloseRequested() override { StopRuntimeHealth(); return true; }
  void OnGuestThreadExit(rex::system::XThread*) override { StopRuntimeHealth(); }
  void OnShutdown() override { StopRuntimeHealth(); }
};
