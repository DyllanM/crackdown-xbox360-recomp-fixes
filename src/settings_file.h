#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace crackdown {

enum class SettingsFileState {
  kMissing,
  kValid,
  kRepaired,     // lines that were not valid TOML were removed; the original is kept in `backup`
  kQuarantined,  // too damaged to repair; moved to `backup` and the defaults apply
  kUnreadable,   // left untouched
};

struct SettingsFileResult {
  SettingsFileState state = SettingsFileState::kMissing;
  std::filesystem::path backup;
  std::vector<std::string> dropped;  // "line N: <text>" for each removed line
  std::string detail;
};

// ReXGlue discards a whole config file when one line is not valid TOML. This makes the player's file loadable
// without losing its valid settings: the original is always kept before anything is rewritten.
SettingsFileResult CheckPlayerSettings(const std::filesystem::path& file);

// Writes through a temporary file in the same folder and renames it over `file`.
bool WriteFileAtomic(const std::filesystem::path& file, std::string_view text, std::string* error = nullptr);

}  // namespace crackdown
