// Launcher header art resolves beside the installed program, never from the current working directory.

#include <filesystem>
#include <string>

#include "check.h"
#include "launcher_assets.h"

namespace {

void TestInstalledRoot() {
  check::TempDir temp;
  const std::filesystem::path install = temp.path() / "install root";
  const std::filesystem::path art = install / "assets" / "launcher" / "header-art.png";
  check::WriteText(art, "header art");

  const std::filesystem::path elsewhere = temp.path() / "elsewhere";
  std::filesystem::create_directories(elsewhere);
  const std::filesystem::path before = std::filesystem::current_path();
  std::filesystem::current_path(elsewhere);
  const std::filesystem::path resolved = crackdown::LauncherHeaderArtPath(install);
  std::filesystem::current_path(before);

  CHECK(resolved == art);
  CHECK(resolved.is_absolute());
  CHECK(std::filesystem::exists(resolved));
  CHECK(resolved != elsewhere / "assets" / "launcher" / "header-art.png");
}

void TestWiring() {
  const std::filesystem::path src = CRACKDOWN_SRC_DIR;
  const std::string launcher = check::ReadText(src / "launcher.cpp");
  CHECK(launcher.find("LauncherHeaderArtPath(rex::filesystem::GetExecutableFolder())") != std::string::npos);

  const std::string app = check::ReadText(src / "crackdown_app.h");
  CHECK(app.find("game_settings_, {nullptr, immediate_drawer(), nullptr}") != std::string::npos);
}

}  // namespace

int main() {
  TestInstalledRoot();
  TestWiring();
  return check::Finish("launcher_assets_tests");
}
