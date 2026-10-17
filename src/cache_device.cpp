#include "cache_device.h"

#include <cctype>
#include <memory>
#include <string>
#include <system_error>

#include <rex/filesystem/vfs.h>
#include <rex/logging.h>

namespace {

constexpr std::string_view kCacheMountPath = "\\Device\\cache0";
constexpr std::string_view kTitleFolder = "4D5307DC";
constexpr std::string_view kForbiddenNameChars = ":*?\"<>|";

bool IsReservedDeviceName(std::string_view part) {
  std::string stem(part.substr(0, part.find('.')));
  while (!stem.empty() && stem.back() == ' ') {
    stem.pop_back();
  }
  for (char& c : stem) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") {
    return true;
  }
  return stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) &&
         stem[3] >= '1' && stem[3] <= '9';
}

bool IsDotsOnly(std::string_view part) {
  return !part.empty() && part.find_first_not_of('.') == std::string_view::npos;
}

bool IsSafeCacheComponent(std::string_view part) {
  if (IsDotsOnly(part) || IsReservedDeviceName(part)) {
    return false;
  }
  for (const char c : part) {
    if (static_cast<unsigned char>(c) < 0x20 || kForbiddenNameChars.find(c) != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

// HostPathDevice joins each guest component onto the host path unchecked, so ".." or a
// drive name such as "C:" would leave the cache folder.
bool IsSafeCachePath(std::string_view path) {
  size_t start = 0;
  while (true) {
    const size_t separator = path.find_first_of("\\/", start);
    const size_t end = separator == std::string_view::npos ? path.size() : separator;
    if (!IsSafeCacheComponent(path.substr(start, end - start))) {
      return false;
    }
    if (separator == std::string_view::npos) {
      return true;
    }
    start = separator + 1;
  }
}

}  // namespace

CrackdownCacheDevice::CrackdownCacheDevice(const std::filesystem::path& host_path)
    : HostPathDevice(kCacheMountPath, host_path, /*read_only=*/false, /*allow_share_delete=*/true) {}

rex::filesystem::Entry* CrackdownCacheDevice::ResolvePath(const std::string_view path) {
  return IsSafeCachePath(path) ? HostPathDevice::ResolvePath(path) : nullptr;
}

bool MountCacheDevice(rex::filesystem::VirtualFileSystem* file_system,
                      const std::filesystem::path& user_data_root) {
  const auto host_path = user_data_root / kTitleFolder / "cache";
  std::error_code ec;
  std::filesystem::create_directories(host_path, ec);
  if (ec) {
    REXLOG_ERROR("Cache device: cannot create {}: {}", host_path.string(), ec.message());
    return false;
  }

  auto device = std::make_unique<CrackdownCacheDevice>(host_path);
  if (!device->Initialize() || !file_system->RegisterDevice(std::move(device))) {
    REXLOG_ERROR("Cache device: failed to mount {} at {}", kCacheMountPath, host_path.string());
    return false;
  }

  REXLOG_INFO("Cache device: mounted {} at {}", kCacheMountPath, host_path.string());
  return true;
}
