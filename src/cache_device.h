#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>

#include <rex/filesystem/devices/host_path_device.h>

namespace rex::filesystem {
class VirtualFileSystem;
}

// The title's XAM mount task calls ObCreateSymbolicLink("cache:", "\Device\cache0"),
// so that device must exist before the game runs.
class CrackdownCacheDevice : public rex::filesystem::HostPathDevice {
 public:
  explicit CrackdownCacheDevice(const std::filesystem::path& host_path);

  rex::filesystem::Entry* ResolvePath(const std::string_view path) override;

 private:
  std::unordered_set<std::string> missing_paths_;
  std::unordered_set<std::string> rejected_paths_;
};

bool MountCacheDevice(rex::filesystem::VirtualFileSystem* file_system,
                      const std::filesystem::path& user_data_root);
