#include "settings_file.h"

#include <fstream>
#include <iterator>
#include <sstream>

#include <toml++/toml.hpp>

namespace crackdown {

namespace {

constexpr uintmax_t kMaxSettingsBytes = 1024 * 1024;
constexpr int kMaxBackups = 9;

bool IsValidToml(std::string_view text) {
  try {
    (void)toml::parse(text);
    return true;
  } catch (const toml::parse_error&) {
    return false;
  }
}

std::vector<std::string_view> SplitLines(std::string_view text) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  while (start < text.size()) {
    const size_t end = text.find('\n', start);
    const size_t next = end == std::string_view::npos ? text.size() : end + 1;
    lines.push_back(text.substr(start, next - start));
    start = next;
  }
  return lines;
}

std::string_view Trim(std::string_view text) {
  const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!text.empty() && blank(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && blank(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

bool LineIsValid(std::string_view line) {
  const std::string_view content = Trim(line);
  return content.empty() || content.front() == '#' || IsValidToml(content);
}

std::filesystem::path BackupPath(const std::filesystem::path& file, const char* suffix) {
  std::error_code ec;
  for (int i = 0; i < kMaxBackups; ++i) {
    std::filesystem::path candidate = file;
    candidate += suffix;
    if (i) {
      candidate += std::to_string(i);
    }
    if (!std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }
  std::filesystem::path oldest = file;
  oldest += suffix;
  return oldest;
}

bool ReadFile(const std::filesystem::path& file, std::string& text) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return false;
  }
  text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !in.bad();
}

}  // namespace

bool WriteFileAtomic(const std::filesystem::path& file, std::string_view text, std::string* error) {
  std::filesystem::path temp = file;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(text.data(), std::streamsize(text.size()));
    out.flush();
    if (!out) {
      if (error) {
        *error = "cannot write " + temp.string();
      }
      std::error_code ignored;
      std::filesystem::remove(temp, ignored);
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(temp, file, ec);
  if (ec) {
    if (error) {
      *error = ec.message();
    }
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

SettingsFileResult CheckPlayerSettings(const std::filesystem::path& file) {
  SettingsFileResult result;
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) {
    return result;
  }
  if (!std::filesystem::is_regular_file(file, ec)) {
    result.state = SettingsFileState::kUnreadable;
    result.detail = "not a regular file";
    return result;
  }
  const uintmax_t size = std::filesystem::file_size(file, ec);
  std::string text;
  if (ec || size > kMaxSettingsBytes || !ReadFile(file, text)) {
    result.state = SettingsFileState::kUnreadable;
    result.detail = ec ? ec.message() : size > kMaxSettingsBytes ? "larger than 1 MiB" : "cannot be read";
    return result;
  }
  if (IsValidToml(text)) {
    result.state = SettingsFileState::kValid;
    return result;
  }

  std::string repaired;
  int line_number = 0;
  for (const std::string_view line : SplitLines(text)) {
    ++line_number;
    if (LineIsValid(line)) {
      repaired.append(line);
    } else {
      result.dropped.push_back("line " + std::to_string(line_number) + ": " + std::string(Trim(line)));
    }
  }

  if (IsValidToml(repaired)) {
    result.backup = BackupPath(file, ".bak");
    std::filesystem::copy_file(file, result.backup, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      result.state = SettingsFileState::kUnreadable;
      result.detail = "cannot keep a backup: " + ec.message();
      result.backup.clear();
      result.dropped.clear();
      return result;
    }
    std::string error;
    if (!WriteFileAtomic(file, repaired, &error)) {
      result.state = SettingsFileState::kUnreadable;
      result.detail = error;
      result.dropped.clear();
      return result;
    }
    result.state = SettingsFileState::kRepaired;
    return result;
  }

  result.dropped.clear();
  result.backup = BackupPath(file, ".invalid");
  std::filesystem::rename(file, result.backup, ec);
  if (ec) {
    result.state = SettingsFileState::kUnreadable;
    result.detail = "cannot move the file aside: " + ec.message();
    result.backup.clear();
    return result;
  }
  result.state = SettingsFileState::kQuarantined;
  return result;
}

}  // namespace crackdown
