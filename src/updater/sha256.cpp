#include "updater/sha256.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

#include "../sha256.h"

namespace trg::update {
namespace {

std::string LowerHex(const crackdown::Sha256Digest& digest) {
  std::string text = crackdown::ToHex(digest);
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

}  // namespace

std::string Sha256Hex(std::string_view data) {
  crackdown::Sha256 hash;
  hash.Update(data.data(), data.size());
  return LowerHex(hash.Finish());
}

std::string Sha256FileHex(const std::filesystem::path& path, std::string& error) {
  error.clear();
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "Cannot read " + path.string() + ".";
    return {};
  }

  crackdown::Sha256 hash;
  std::vector<char> buffer(1 << 16);
  while (file) {
    file.read(buffer.data(), std::streamsize(buffer.size()));
    const std::streamsize count = file.gcount();
    if (count > 0) {
      hash.Update(buffer.data(), size_t(count));
    }
  }
  if (!file.eof()) {
    error = "Cannot read " + path.string() + ".";
    return {};
  }
  return LowerHex(hash.Finish());
}

bool IsValidSha256(std::string_view text) {
  return text.size() == 64 &&
         std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string NormalizeSha256(std::string_view text) {
  std::string normalized(text);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return normalized;
}

}  // namespace trg::update
