#include "user_data.h"

#include <fstream>

namespace crackdown {

namespace {

constexpr const char* kUserDataFolder = "user_data";
constexpr const char* kProbeFile = ".write_test";

bool DefaultProbe(const std::filesystem::path& folder, std::error_code& ec) {
  const std::filesystem::path probe = folder / kProbeFile;
  {
    std::ofstream out(probe, std::ios::binary | std::ios::trunc);
    if (!out) {
      ec = std::make_error_code(std::errc::permission_denied);
      return false;
    }
    out << 'x';
    if (!out.flush()) {
      ec = std::make_error_code(std::errc::io_error);
      return false;
    }
  }
  std::filesystem::remove(probe, ec);
  return !ec;
}

UserDataSelection NotWritable(const std::filesystem::path& root, std::string detail) {
  return {UserDataOutcome::kNotWritable, root, std::move(detail)};
}

}  // namespace

UserDataSelection SelectUserData(const UserDataRequest& request, const UserDataFileOps& ops) {
  if (!request.explicit_root.empty()) {
    return {UserDataOutcome::kExplicit, request.explicit_root, {}};
  }

  const std::filesystem::path root = request.port_folder / kUserDataFolder;
  std::error_code ec;
  const bool exists = std::filesystem::exists(root, ec);
  if (exists && !std::filesystem::is_directory(root, ec)) {
    return NotWritable(root, "it exists and is not a folder");
  }
  if (!exists && !ops.create_directories(root, ec)) {
    return NotWritable(root, ec.message());
  }
  const bool writable = ops.probe_writable ? ops.probe_writable(root, ec) : DefaultProbe(root, ec);
  if (!writable) {
    return NotWritable(root, ec ? ec.message() : "it cannot be written");
  }
  return {UserDataOutcome::kPortable, root, {}};
}

UserDataMessage DescribeUserData(const UserDataSelection& selection) {
  switch (selection.outcome) {
    case UserDataOutcome::kExplicit:
      return {false, "User data: " + selection.root.string() + " (--user_data_root)"};
    case UserDataOutcome::kNotWritable:
      return {true, "User data: " + selection.root.string() + " cannot be used: " + selection.detail};
    case UserDataOutcome::kPortable:
      break;
  }
  return {false, "User data: " + selection.root.string()};
}

std::string UserDataNotWritableText(const UserDataSelection& selection) {
  return "Crackdown cannot save its settings and progress because it cannot write to:\n\n" +
         selection.root.string() +
         "\n\nMove the Crackdown folder to a place you can write to, such as a games folder in your "
         "user profile, and start it again.";
}

}  // namespace crackdown
