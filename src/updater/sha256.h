#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace trg::update {

std::string Sha256Hex(std::string_view data);
std::string Sha256FileHex(const std::filesystem::path& path, std::string& error);

bool IsValidSha256(std::string_view text);
std::string NormalizeSha256(std::string_view text);

}  // namespace trg::update
