#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
#include "launcher_quit.h"

namespace {

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(CRACKDOWN_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestDecision() {
  CHECK(crackdown::LauncherQuitAllowed(false));
  CHECK(!crackdown::LauncherQuitAllowed(true));
}

void TestQuitWiring() {
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "crackdown::LauncherQuitAllowed(ImGui::IsKeyPressed(ImGuiKey_Escape, false))"));
  CHECK(Has(launcher, "Finish(cb_.quit);"));
  CHECK(Has(launcher, "c.hints = \"Enter  Play     Alt+F4  Quit     Ctrl+S  Save\";"));
}

void TestCaptureAndDialogEscape() {
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "const std::optional<ImGuiKey> key = ui.KeyBindRow(action.label, nullptr, shown);"));
  CHECK(Has(launcher, "if (!key) {"));
  CHECK(Has(launcher, "Click a key, then press the new one. Esc cancels."));
}

void TestFallbackLauncher() {
  const std::string platform = ReadSource("platform.cpp");
  CHECK(!Has(platform, "SDLK_ESCAPE"));
  CHECK(Has(platform, "SDL_EVENT_WINDOW_CLOSE_REQUESTED"));
  CHECK(Has(platform, "FallbackButton::kQuit"));
}

}  // namespace

int main() {
  TestDecision();
  TestQuitWiring();
  TestCaptureAndDialogEscape();
  TestFallbackLauncher();
  return check::Finish("launcher_quit_tests");
}
