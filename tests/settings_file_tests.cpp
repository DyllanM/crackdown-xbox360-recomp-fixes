#include "check.h"
#include "settings_file.h"

using crackdown::CheckPlayerSettings;
using crackdown::SettingsFileState;

namespace {

void TestMissingAndValid() {
  check::TempDir dir;
  const auto file = dir.path() / "settings.toml";
  CHECK(CheckPlayerSettings(file).state == SettingsFileState::kMissing);

  const std::string text = "# header\nwindow_width = 1280\nfullscreen = true\nname = \"a b\"\n";
  check::WriteText(file, text);
  const auto result = CheckPlayerSettings(file);
  CHECK(result.state == SettingsFileState::kValid);
  CHECK(check::ReadText(file) == text);
  CHECK(!std::filesystem::exists(dir.path() / "settings.toml.bak"));
}

void TestBareWordsAreDroppedAndOthersKept() {
  check::TempDir dir;
  const auto file = dir.path() / "settings.toml";
  const std::string original = "window_width = 1280\nfullscreen = on\nmonitor = 2\nvsync = off\n";
  check::WriteText(file, original);
  const auto result = CheckPlayerSettings(file);
  CHECK(result.state == SettingsFileState::kRepaired);
  CHECK(result.dropped.size() == 2);
  CHECK(check::ReadText(file) == "window_width = 1280\nmonitor = 2\n");
  CHECK(check::ReadText(result.backup) == original);
  CHECK(CheckPlayerSettings(file).state == SettingsFileState::kValid);
}

void TestCommentsTablesAndLineEndingsSurvive() {
  check::TempDir dir;
  const auto file = dir.path() / "settings.toml";
  check::WriteText(file, "\xEF\xBB\xBF# note\r\n[display]\r\nwidth = 1920\r\nbroken = = 3\r\nheight = 1080\r\n");
  const auto result = CheckPlayerSettings(file);
  CHECK(result.state == SettingsFileState::kRepaired);
  CHECK(check::ReadText(file) == "\xEF\xBB\xBF# note\r\n[display]\r\nwidth = 1920\r\nheight = 1080\r\n");
}

void TestUnterminatedStringIsDropped() {
  check::TempDir dir;
  const auto file = dir.path() / "settings.toml";
  check::WriteText(file, "a = 1\nname = \"open\nb = 2\n");
  const auto result = CheckPlayerSettings(file);
  CHECK(result.state == SettingsFileState::kRepaired);
  CHECK(check::ReadText(file) == "a = 1\nb = 2\n");
}

void TestUnrepairableFileIsQuarantinedNotDeleted() {
  check::TempDir dir;
  const auto file = dir.path() / "settings.toml";
  const std::string original = "a = 1\na = 2\n";
  check::WriteText(file, original);
  const auto result = CheckPlayerSettings(file);
  CHECK(result.state == SettingsFileState::kQuarantined);
  CHECK(!std::filesystem::exists(file));
  CHECK(check::ReadText(result.backup) == original);
}

void TestBackupsAreNotOverwritten() {
  check::TempDir dir;
  const auto file = dir.path() / "settings.toml";
  check::WriteText(file, "a = on\n");
  const auto first = CheckPlayerSettings(file);
  check::WriteText(file, "b = off\n");
  const auto second = CheckPlayerSettings(file);
  CHECK(first.backup != second.backup);
  CHECK(check::ReadText(first.backup) == "a = on\n");
  CHECK(check::ReadText(second.backup) == "b = off\n");
}

void TestUnreadableFileIsLeftAlone() {
  check::TempDir dir;
  const auto folder = dir.path() / "settings.toml";
  std::filesystem::create_directories(folder);
  const auto result = CheckPlayerSettings(folder);
  CHECK(result.state == SettingsFileState::kUnreadable);
  CHECK(std::filesystem::is_directory(folder));
}

void TestAtomicWrite() {
  check::TempDir dir;
  const auto file = dir.path() / "settings.toml";
  std::string error;
  CHECK(crackdown::WriteFileAtomic(file, "x = 1\n", &error));
  CHECK(crackdown::WriteFileAtomic(file, "x = 2\n", &error));
  CHECK(check::ReadText(file) == "x = 2\n");
  CHECK(!std::filesystem::exists(dir.path() / "settings.toml.tmp"));
  CHECK(!crackdown::WriteFileAtomic(dir.path() / "missing" / "settings.toml", "x", &error));
  CHECK(!error.empty());
}

}  // namespace

int main() {
  TestMissingAndValid();
  TestBareWordsAreDroppedAndOthersKept();
  TestCommentsTablesAndLineEndingsSurvive();
  TestUnterminatedStringIsDropped();
  TestUnrepairableFileIsQuarantinedNotDeleted();
  TestBackupsAreNotOverwritten();
  TestUnreadableFileIsLeftAlone();
  TestAtomicWrite();
  return check::Finish("settings_file_tests");
}
