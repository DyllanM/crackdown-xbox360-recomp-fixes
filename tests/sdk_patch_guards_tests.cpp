#include <filesystem>
#include <string>

#include "check.h"

namespace {

std::string ReadPatch(const char* prefix) {
  for (const auto& entry : std::filesystem::directory_iterator(CRACKDOWN_PATCH_DIR)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind(prefix, 0) == 0) return check::ReadText(entry.path());
  }
  return {};
}

bool HasLine(const std::string& text, const std::string& line) {
  size_t at = 0;
  while ((at = text.find(line, at)) != std::string::npos) {
    const size_t end = at + line.size();
    const bool starts_line = at == 0 || text[at - 1] == '\n';
    const bool ends_line = end == text.size() || text[end] == '\n' || text[end] == '\r';
    if (starts_line && ends_line) return true;
    at = end;
  }
  return false;
}

void TestXamEnumerateCountStartsAtZero() {
  const std::string patch = ReadPatch("0060-");
  CHECK(!patch.empty());
  CHECK(HasLine(patch, "-  uint32_t dummy;"));
  CHECK(HasLine(patch, "+  uint32_t dummy = 0;"));
  CHECK(patch.find("src/kernel/xam/xam_enum.cpp") != std::string::npos);
}

void TestPatchedSourceHasNoUninitialisedCount() {
  const std::filesystem::path source =
      std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../../out/rexglue-sdk-src/src/kernel/xam/xam_enum.cpp";
  if (!std::filesystem::exists(source)) return;
  const std::string text = check::ReadText(source);
  CHECK(text.find("uint32_t dummy = 0;") != std::string::npos);
  CHECK(text.find("uint32_t dummy;") == std::string::npos);

  const size_t body = text.find("uint32_t xeXamEnumerate(");
  const size_t body_end = text.find("\n}\n", body);
  CHECK(body != std::string::npos && body_end != std::string::npos);
  const std::string enumerate = text.substr(body, body_end - body);
  const size_t zeroed = enumerate.find("*items_returned = 0;");
  const size_t first_return = enumerate.find("return ");
  CHECK(zeroed != std::string::npos && zeroed < first_return);
  CHECK(enumerate.find("*items_returned = result == X_ERROR_SUCCESS ? item_count : 0;") != std::string::npos);
}

void TestMassiveCountHookIsWired() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string overrides = check::ReadText(root / "config/overrides.toml");
  const size_t hook = overrides.find("address = 0x82AA0A6C");
  CHECK(hook != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_massive_server_count_guard\"", hook) != std::string::npos);
  const std::string cmake = check::ReadText(root / "CMakeLists.txt");
  CHECK(cmake.find("src/massive_server_guard.cpp") != std::string::npos);
}

void TestInvalidFetchPatchOnlyReplacesTheWarning() {
  const std::string patch = ReadPatch("0064-");
  CHECK(!patch.empty());
  const char* const warning_parts[] = {
      "REXGPU_WARN(", "\"Texture fetch constant (", "\"has \\\"invalid\\\" type!",
      "\"bypassing this by launching Xenia with", "\"--gpu_allow_invalid_fetch_constants=true.\"",
      "fetch.dword_0, fetch.dword_1"};
  size_t removed = 0;
  size_t line_start = 0;
  while (line_start < patch.size()) {
    size_t line_end = patch.find('\n', line_start);
    if (line_end == std::string::npos) line_end = patch.size();
    const std::string line = patch.substr(line_start, line_end - line_start);
    line_start = line_end + 1;
    if (line.rfind("-", 0) != 0 || line.rfind("---", 0) == 0) continue;
    ++removed;
    bool is_warning = false;
    for (const char* part : warning_parts) {
      is_warning = is_warning || line.find(part) != std::string::npos;
    }
    CHECK(is_warning);
  }
  CHECK(removed == 6);
  CHECK(HasLine(patch, "       if (REXCVAR_GET(gpu_allow_invalid_fetch_constants)) {"));
  CHECK(HasLine(patch, "       return;"));
}

}  // namespace

int main() {
  TestXamEnumerateCountStartsAtZero();
  TestPatchedSourceHasNoUninitialisedCount();
  TestMassiveCountHookIsWired();
  TestInvalidFetchPatchOnlyReplacesTheWarning();
  return check::Finish("sdk_patch_guards_tests");
}
