// Relaunch: which program the launcher starts again, and with which arguments.

#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "relaunch_command.h"

using crackdown::MakeRelaunchCommand;

namespace {

using Args = std::vector<std::string>;

constexpr const char* kMountedProgram = "/tmp/.mount_CrackdAbCdEf/usr/bin/crackdown";
constexpr const char* kAppImage =
    "/home/deck/Games/Crackdown/Crackdown-Recompiled-v0.1.0-x86_64.AppImage";
constexpr const char* kSkipLauncher = "--crackdown_skip_launcher=true";

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestAppImageSet() {
  const auto command = MakeRelaunchCommand({kMountedProgram}, kAppImage, kMountedProgram, kSkipLauncher);
  CHECK(command.has_value());
  CHECK(command->executable == kAppImage);  // the file that was started, not the closing mount
  CHECK((command->argv == Args{kAppImage, kSkipLauncher}));
  CHECK(!Has(command->executable, ".mount_"));
  CHECK(!Has(command->argv.front(), ".mount_"));
}

void TestAppImageUnset() {
  const std::string program = "/home/user/my games/crackdown/crackdown";
  for (const char* appimage : {static_cast<const char*>(nullptr), ""}) {
    const auto command = MakeRelaunchCommand({"./crackdown"}, appimage, program, kSkipLauncher);
    CHECK(command.has_value());
    CHECK(command->executable == program);
    CHECK((command->argv == Args{program, kSkipLauncher}));
  }
}

void TestArguments() {
  const Args original{kMountedProgram, "--user_data_root=/data/with space", "", "--fullscreen=true"};
  const auto command = MakeRelaunchCommand(original, kAppImage, kMountedProgram, kSkipLauncher);
  CHECK(command.has_value());
  CHECK((command->argv ==
         Args{kAppImage, "--user_data_root=/data/with space", "", "--fullscreen=true", kSkipLauncher}));

  // A restart that was itself started with the flag keeps it once more at the end; the cvar parser takes the last.
  const auto again = MakeRelaunchCommand({kMountedProgram, kSkipLauncher}, kAppImage, kMountedProgram, kSkipLauncher);
  CHECK(again.has_value());
  CHECK((again->argv == Args{kAppImage, kSkipLauncher, kSkipLauncher}));
}

void TestWorkingDirectory() {
  const auto expected = MakeRelaunchCommand({kMountedProgram}, kAppImage, kMountedProgram, kSkipLauncher);
  std::error_code ec;
  const std::filesystem::path before = std::filesystem::current_path(ec);
  std::filesystem::current_path(std::filesystem::temp_directory_path(), ec);
  const auto elsewhere = MakeRelaunchCommand({kMountedProgram}, kAppImage, kMountedProgram, kSkipLauncher);
  std::filesystem::current_path(before, ec);
  CHECK(expected.has_value() && elsewhere.has_value());
  CHECK(expected->executable == elsewhere->executable);
  CHECK(expected->argv == elsewhere->argv);
  CHECK(elsewhere->executable.front() == '/');
}

void TestUnreadableCommandLine() {
  CHECK(!MakeRelaunchCommand({}, kAppImage, kMountedProgram, kSkipLauncher).has_value());
  CHECK(!MakeRelaunchCommand({"x"}, nullptr, "", kSkipLauncher).has_value());
  CHECK(!MakeRelaunchCommand({}, nullptr, "", kSkipLauncher).has_value());
}

void TestWiring() {
  const std::filesystem::path src = CRACKDOWN_SRC_DIR;
  const std::string platform = check::ReadText(src / "platform.cpp");
  CHECK(Has(platform, "MakeRelaunchCommand(std::move(args), std::getenv(\"APPIMAGE\")"));
  CHECK(Has(platform, "posix_spawn(&pid, command->executable.c_str()"));  // no shell, no command string
  CHECK(!Has(platform, "system("));
  CHECK(!Has(platform, "popen("));

  const std::string app = check::ReadText(src / "crackdown_app.h");
  CHECK(Has(app, "crackdown::RelaunchSelf(L\"--crackdown_skip_launcher=true\")"));

  const std::string apprun = check::ReadText(src / ".." / "packaging" / "linux" / "AppRun");
  CHECK(Has(apprun, "HERE=\"$(dirname \"$(readlink -f \"$0\")\")\""));  // not the working directory
  CHECK(Has(apprun, "exec \"$HERE/usr/bin/crackdown\" \"$@\""));       // quoted, arguments untouched
}

}  // namespace

int main() {
  TestAppImageSet();
  TestAppImageUnset();
  TestArguments();
  TestWorkingDirectory();
  TestUnreadableCommandLine();
  TestWiring();
  return check::Finish("relaunch_tests");
}
