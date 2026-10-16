#include "check.h"
#include "user_data.h"

using crackdown::SelectUserData;
using crackdown::UserDataFileOps;
using crackdown::UserDataOutcome;
using crackdown::UserDataRequest;

namespace {

void TestFirstRunCreatesPortableFolder() {
  check::TempDir dir;
  const auto selection = SelectUserData({dir.path(), {}});
  CHECK(selection.outcome == UserDataOutcome::kPortable);
  CHECK(selection.root == dir.path() / "user_data");
  CHECK(std::filesystem::is_directory(selection.root));
  CHECK(!std::filesystem::exists(selection.root / ".write_test"));
}

void TestExistingDataIsKept() {
  check::TempDir dir;
  check::WriteText(dir.path() / "user_data" / "4D5307DC" / "profile.bin", "save");
  const auto selection = SelectUserData({dir.path(), {}});
  CHECK(selection.outcome == UserDataOutcome::kPortable);
  CHECK(check::ReadText(dir.path() / "user_data" / "4D5307DC" / "profile.bin") == "save");
}

void TestExplicitRootIsUsedAsGiven() {
  check::TempDir dir;
  const auto selection = SelectUserData({dir.path(), dir.path() / "elsewhere"});
  CHECK(selection.outcome == UserDataOutcome::kExplicit);
  CHECK(selection.root == dir.path() / "elsewhere");
  CHECK(!std::filesystem::exists(dir.path() / "user_data"));
  CHECK(!std::filesystem::exists(dir.path() / "elsewhere"));
}

void TestFileInTheWayStopsStartup() {
  check::TempDir dir;
  check::WriteText(dir.path() / "user_data", "not a folder");
  const auto selection = SelectUserData({dir.path(), {}});
  CHECK(selection.outcome == UserDataOutcome::kNotWritable);
  CHECK(!selection.detail.empty());
  CHECK(check::ReadText(dir.path() / "user_data") == "not a folder");
}

void TestUncreatableFolderStopsStartup() {
  check::TempDir dir;
  UserDataFileOps ops;
  ops.create_directories = [](const std::filesystem::path&, std::error_code& ec) {
    ec = std::make_error_code(std::errc::permission_denied);
    return false;
  };
  const auto selection = SelectUserData({dir.path(), {}}, ops);
  CHECK(selection.outcome == UserDataOutcome::kNotWritable);
  CHECK(crackdown::DescribeUserData(selection).warning);
  CHECK(crackdown::UserDataNotWritableText(selection).find("user_data") != std::string::npos);
}

void TestUnwritableFolderStopsStartup() {
  check::TempDir dir;
  UserDataFileOps ops;
  ops.probe_writable = [](const std::filesystem::path&, std::error_code& ec) {
    ec = std::make_error_code(std::errc::read_only_file_system);
    return false;
  };
  const auto selection = SelectUserData({dir.path(), {}}, ops);
  CHECK(selection.outcome == UserDataOutcome::kNotWritable);
}

void TestMessages() {
  check::TempDir dir;
  const auto portable = crackdown::DescribeUserData(SelectUserData({dir.path(), {}}));
  CHECK(!portable.warning);
  CHECK(portable.text.find("user_data") != std::string::npos);
  const auto explicit_root = crackdown::DescribeUserData(SelectUserData({dir.path(), dir.path() / "x"}));
  CHECK(explicit_root.text.find("--user_data_root") != std::string::npos);
}

}  // namespace

int main() {
  TestFirstRunCreatesPortableFolder();
  TestExistingDataIsKept();
  TestExplicitRootIsUsedAsGiven();
  TestFileInTheWayStopsStartup();
  TestUncreatableFolderStopsStartup();
  TestUnwritableFolderStopsStartup();
  TestMessages();
  return check::Finish("user_data_tests");
}
