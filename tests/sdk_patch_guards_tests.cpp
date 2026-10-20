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

void TestXamLaunchTitleOwnsPath() {
  const std::string patch = ReadPatch("0086-");
  CHECK(!patch.empty());
  CHECK(HasLine(patch, "-    auto path = raw_name_ptr.value();"));
  CHECK(HasLine(patch, "+    std::string path(raw_name_ptr.value());"));
  CHECK(patch.find("src/kernel/xam/xam_info.cpp") != std::string::npos);
}

void TestCompareStringNUsesRealLengths() {
  const std::string patch = ReadPatch("0087-");
  CHECK(!patch.empty());
  CHECK(HasLine(patch, "-  auto len = std::min(string_1_len, string_2_len);"));
  CHECK(HasLine(patch, "+  auto len = std::min(len1, len2);"));
  CHECK(HasLine(patch, "+    ret = int(len1) - int(len2);"));
}

void TestScissorEdgesCompareSigned() {
  const std::string patch = ReadPatch("0089-");
  CHECK(!patch.empty());
  CHECK(HasLine(patch, "+        if (scissor_bottom >= int32_t(xenos::kTexture2DCubeMaxWidthHeight)) {"));
  CHECK(HasLine(patch, "+          if (scissor_right >= int32_t(xenos::kTexture2DCubeMaxWidthHeight)) {"));
}

void TestD3D12TextureSliceSizeInitialised() {
  const std::string patch = ReadPatch("0088-");
  CHECK(!patch.empty());
  CHECK(HasLine(patch, "-  UINT64 host_slice_size_base;"));
  CHECK(HasLine(patch, "+  UINT64 host_slice_size_base = 0;"));
}

void TestUnusedResolveCountersRemoved() {
  const std::string patch = ReadPatch("0090-");
  CHECK(!patch.empty());
  CHECK(HasLine(patch, "-  uint64_t direct_resolve_success_count_ = 0;"));
  CHECK(HasLine(patch, "-  uint64_t direct_resolve_fallback_count_ = 0;"));
}

void TestMakePacketType0SetsOneRegister() {
  const std::string patch = ReadPatch("0091-");
  CHECK(!patch.empty());
  CHECK(HasLine(patch,
                "+  return (0u << 30) | (((count - 1) & 0x3FFF) << 16) | (uint32_t(one_reg) << 15) | (index & 0x7FFF);"));
}

void TestUnusedLocalsKeepGuestReads() {
  const std::string patch = ReadPatch("0093-");
  CHECK(!patch.empty());
  CHECK(HasLine(patch, "+  [[maybe_unused]] auto vftable_ptr = *reinterpret_cast<rex::be<uint32_t>*>(thrown);"));
  CHECK(HasLine(patch, "+[[maybe_unused]] static const char* LifecycleBadge(rex::cvar::Lifecycle lc) {"));
  CHECK(HasLine(patch, "-  uint8_t rex_w = rex & 0b1000;"));
}

void TestLedgeSnapDownHookIsWired() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string overrides = check::ReadText(root / "config/overrides.toml");
  const size_t hook = overrides.find("address = 0x822B5634");
  CHECK(hook != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_ledge_snap_down\"", hook) != std::string::npos);
  CHECK(overrides.find("registers = [\"f0\"]", hook) != std::string::npos);
  CHECK(overrides.find("after_instruction = false", hook) != std::string::npos);
  const std::string cmake = check::ReadText(root / "CMakeLists.txt");
  CHECK(cmake.find("src/character_step_fixes.cpp") != std::string::npos);
}

void TestRagdollPoseDtHookIsWired() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string overrides = check::ReadText(root / "config/overrides.toml");
  const size_t hook = overrides.find("address = 0x822BA2E8");
  CHECK(hook != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_ragdoll_pose_dt\"", hook) != std::string::npos);
  CHECK(overrides.find("registers = [\"f1\", \"r29\"]", hook) != std::string::npos);
  CHECK(overrides.find("after_instruction = false", hook) != std::string::npos);
  const std::string cmake = check::ReadText(root / "CMakeLists.txt");
  CHECK(cmake.find("src/character_step_fixes.cpp") != std::string::npos);
  const std::string source = check::ReadText(root / "src/character_step_fixes.cpp");
  CHECK(source.find("void crackdown_ragdoll_pose_dt") != std::string::npos);
}

void TestCameraLiftDivisorHookIsWired() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string overrides = check::ReadText(root / "config/overrides.toml");
  const size_t hook = overrides.find("address = 0x8228FDDC");
  CHECK(hook != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_camera_lift_divisor\"", hook) != std::string::npos);
  CHECK(overrides.find("registers = [\"f13\"]", hook) != std::string::npos);
  CHECK(overrides.find("after_instruction = false", hook) != std::string::npos);
  const std::string source = check::ReadText(root / "src/character_step_fixes.cpp");
  CHECK(source.find("void crackdown_camera_lift_divisor") != std::string::npos);
}

void TestCameraRollbackHookIsWired() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string source = check::ReadText(root / "src/nan_guards.cpp");
  CHECK(source.find("REX_EXTERN(__imp__sub_82287130);") != std::string::npos);
  CHECK(source.find("REX_HOOK_RAW(sub_82287130)") != std::string::npos);
  CHECK(source.find("crackdown::guest::Plausible(camera)") != std::string::npos);
  CHECK(source.find("if (plausible)") != std::string::npos);
  CHECK(source.find("if (!plausible)") != std::string::npos);
  CHECK(source.find("crackdown::nan_guard::kCameraBytes") != std::string::npos);
  const size_t call = source.find("__imp__sub_82287130(ctx, base);");
  CHECK(call != std::string::npos);
  CHECK(source.find("__imp__sub_82287130(ctx, base);", call + 1) == std::string::npos);
  const std::string cmake = check::ReadText(root / "CMakeLists.txt");
  CHECK(cmake.find("src/nan_guards.cpp") != std::string::npos);
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
  TestLedgeSnapDownHookIsWired();
  TestRagdollPoseDtHookIsWired();
  TestCameraLiftDivisorHookIsWired();
  TestCameraRollbackHookIsWired();
  TestXamLaunchTitleOwnsPath();
  TestCompareStringNUsesRealLengths();
  TestScissorEdgesCompareSigned();
  TestD3D12TextureSliceSizeInitialised();
  TestUnusedResolveCountersRemoved();
  TestMakePacketType0SetsOneRegister();
  TestUnusedLocalsKeepGuestReads();
  TestInvalidFetchPatchOnlyReplacesTheWarning();
  return check::Finish("sdk_patch_guards_tests");
}
