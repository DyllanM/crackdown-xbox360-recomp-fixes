#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <system_error>

namespace crackdown {

enum class UserDataOutcome {
  kPortable,     // <port folder>/user_data
  kExplicit,     // --user_data_root
  kNotWritable,  // <port folder>/user_data cannot be created or written
};

struct UserDataRequest {
  std::filesystem::path port_folder;
  std::filesystem::path explicit_root;  // empty unless --user_data_root was given
};

struct UserDataSelection {
  UserDataOutcome outcome = UserDataOutcome::kPortable;
  std::filesystem::path root;
  std::string detail;  // why the folder is not writable
};

// The file system calls the selection makes, replaceable to test failures.
struct UserDataFileOps {
  std::function<bool(const std::filesystem::path&, std::error_code&)> create_directories =
      [](const std::filesystem::path& path, std::error_code& ec) {
        std::filesystem::create_directories(path, ec);
        return !ec;
      };
  std::function<bool(const std::filesystem::path&, std::error_code&)> probe_writable;
};

// Player data lives beside the program, never in a per-user system folder, so the layout is final from the first
// release. The program stops when the folder cannot be written rather than splitting data across two places.
UserDataSelection SelectUserData(const UserDataRequest& request, const UserDataFileOps& ops = {});

struct UserDataMessage {
  bool warning = false;
  std::string text;
};

UserDataMessage DescribeUserData(const UserDataSelection& selection);

// The text of the error shown when the player data folder cannot be written.
std::string UserDataNotWritableText(const UserDataSelection& selection);

}  // namespace crackdown
