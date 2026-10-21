#include <filesystem>
#include <regex>
#include <set>
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

void TestMainMenuQuitHookIsWired() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string overrides = check::ReadText(root / "config/overrides.toml");
  const size_t handler = overrides.find("address = 0x8267839C");
  CHECK(handler != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_main_menu_quit_use_handler\"", handler) != std::string::npos);
  CHECK(overrides.find("jump_address = 0x826783A0", handler) != std::string::npos);
  const size_t attached = overrides.find("address = 0x826783B4");
  CHECK(attached != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_main_menu_quit_handler_attached\"", attached) != std::string::npos);
  CHECK(overrides.find("jump_address = 0x826783B8", attached) != std::string::npos);
  const size_t hook = overrides.find("address = 0x826783B8");
  CHECK(hook != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_main_menu_quit_enable\"", hook) != std::string::npos);
  CHECK(overrides.find("registers = [\"r4\", \"r11\", \"r31\"]", hook) != std::string::npos);
  CHECK(overrides.find("after_instruction = true", hook) != std::string::npos);
  CHECK(overrides.find("address = 0x826783C0") == std::string::npos);
  const size_t popup = overrides.find("address = 0x82678778");
  CHECK(popup != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_main_menu_quit_popup_state\"", popup) != std::string::npos);
  CHECK(overrides.find("registers = [\"r11\"]", popup) != std::string::npos);
  const std::string source = check::ReadText(root / "src/main_menu_quit.cpp");
  CHECK(source.find("void crackdown_main_menu_quit_use_handler()") != std::string::npos);
  CHECK(source.find("void crackdown_main_menu_quit_handler_attached()") != std::string::npos);
  CHECK(source.find("void crackdown_main_menu_quit_enable(PPCRegister& enabled, PPCRegister& control, "
                    "PPCRegister& self)") != std::string::npos);
  CHECK(source.find("REX_HOOK_RAW(sub_82678768)") != std::string::npos);
  CHECK(source.find("REX_HOOK_RAW(sub_826587F8)") != std::string::npos);
  CHECK(source.find("REX_HOOK_RAW(sub_826586B0)") != std::string::npos);
  CHECK(source.find("REX_HOOK_RAW(sub_82658560)") != std::string::npos);
  CHECK(source.find("REX_HOOK_RAW(sub_82381F28)") != std::string::npos);
  CHECK(source.find("__imp__sub_82690F68") == std::string::npos);
  CHECK(source.find("__imp__sub_8265D630") == std::string::npos);
  CHECK(source.find("__imp__sub_8213BF98") == std::string::npos);
  CHECK(source.find("835D47B0") == std::string::npos);
  CHECK(source.find("QuitConfirmOverlay") == std::string::npos);
  CHECK(!std::filesystem::exists(root / "src/quit_confirm_overlay.cpp"));
  CHECK(!std::filesystem::exists(root / "src/quit_confirm_overlay.h"));
}

void TestMainMenuQuitUsesWindowCloseExit() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string app = check::ReadText(root / "src/crackdown_app.h");
  CHECK(app.find("void OnClosing(rex::ui::UIEvent& e) override") != std::string::npos);
  CHECK(app.find("void RequestHardExitFromUIThread()") != std::string::npos);
  CHECK(app.find("void HardExitFromUIThread()") != std::string::npos);
  CHECK(app.find("SetQuitHandler([this] { RequestHardExitFromUIThread(); })") != std::string::npos);
  CHECK(app.find("std::_Exit(0);") != std::string::npos);
  const std::string quit = check::ReadText(root / "src/main_menu_quit.cpp");
  CHECK(quit.find("DEBUG_SAULO") == std::string::npos);
  CHECK(quit.find("InvokeQuitHandler();") != std::string::npos);
  CHECK(quit.find("QuitFromUIThread") == std::string::npos);
}

void TestMouseLookHooksAreWired() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string overrides = check::ReadText(root / "config/overrides.toml");
  const size_t a = overrides.find("address = 0x824FC15C");
  CHECK(a != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_mouse_look_camera_a\"", a) != std::string::npos);
  CHECK(overrides.find("registers = [\"f25\", \"f31\"]", a) != std::string::npos);
  const size_t b = overrides.find("address = 0x824FABF0");
  CHECK(b != std::string::npos);
  CHECK(overrides.find("name = \"crackdown_mouse_look_camera_b\"", b) != std::string::npos);
  CHECK(overrides.find("registers = [\"f28\", \"f31\"]", b) != std::string::npos);
  const std::string cmake = check::ReadText(root / "CMakeLists.txt");
  CHECK(cmake.find("src/mouse_look.cpp") != std::string::npos);
}

void TestMouseLookPatchSuppressesStick() {
  const std::string patch = ReadPatch("0055-");
  CHECK(!patch.empty());
  CHECK(patch.find("REXCVAR_DEFINE_BOOL(mnk_mouse_stick") != std::string::npos);
  CHECK(patch.find("REXCVAR_GET(mnk_mouse_stick)") != std::string::npos);
  const std::string source = check::ReadText(
      std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../../out/rexglue-sdk-src/src/input/mnk/mnk_input_driver.cpp");
  if (source.empty()) return;
  CHECK(source.find("mnk_mouse_stick") != std::string::npos);
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


struct MidasmHook {
  const char* address;
  const char* name;
  const char* registers;
  bool after;
};

// The block of one [[midasm_hook]] entry, from its address line to the next entry.
std::string HookBlock(const std::string& overrides, const std::string& address) {
  const std::string line = "address = " + address;
  size_t at = 0;
  while ((at = overrides.find(line, at)) != std::string::npos) {
    const size_t next = at + line.size();
    if (next == overrides.size() || overrides[next] == '\n' || overrides[next] == '\r') break;
    at = next;
  }
  if (at == std::string::npos) return {};
  const size_t end = overrides.find("[[midasm_hook]]", at);
  return overrides.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

void TestHighFrameRateHooksAreWired() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string overrides = check::ReadText(root / "config/overrides.toml");
  const std::string fixes = check::ReadText(root / "src/character_step_fixes.cpp");
  const std::string pacing = check::ReadText(root / "src/frame_pacing.cpp");
  const MidasmHook hooks[] = {
      {"0x822A2DB8", "crackdown_ground_snap_decay", "[\"f13\"]", false},
      {"0x822A2E28", "crackdown_ground_snap_lerp", "[\"f13\"]", true},
      {"0x822AA8F4", "crackdown_first_gravity_step", "[\"f0\", \"f13\"]", true},
      {"0x822E191C", "crackdown_hit_merge_window", "[\"r11\"]", true},
      {"0x822BA48C", "crackdown_ragdoll_tick_window", "[\"r10\"]", true},
      {"0x823672B0", "crackdown_projectile_probe_dt", "[\"r1\"]", false},
      {"0x823308DC", "crackdown_crowd_anim_step", "[\"f0\"]", true},
      {"0x823FF534", "crackdown_cosmetic_timer_step", "[\"f13\"]", true},
  };
  for (const MidasmHook& hook : hooks) {
    const std::string block = HookBlock(overrides, hook.address);
    CHECK(!block.empty());
    CHECK(block.find(std::string("name = \"") + hook.name + "\"") != std::string::npos);
    CHECK(block.find(std::string("registers = ") + hook.registers) != std::string::npos);
    CHECK(block.find(hook.after ? "after_instruction = true" : "after_instruction = false") != std::string::npos);
    const std::string definition = std::string("void ") + hook.name + "(";
    CHECK(fixes.find(definition) != std::string::npos || pacing.find(definition) != std::string::npos);
  }
}

// One intentional hook per guest address: codegen would emit two calls at a repeated midasm address.
void TestMidasmAddressesAreUnique() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string overrides = check::ReadText(root / "config/overrides.toml");
  const std::regex address(R"(\naddress = (0x[0-9A-Fa-f]+))");
  std::set<std::string> seen;
  size_t count = 0;
  for (auto it = std::sregex_iterator(overrides.begin(), overrides.end(), address); it != std::sregex_iterator();
       ++it) {
    ++count;
    CHECK(seen.insert((*it)[1].str()).second);
  }
  CHECK(count == seen.size() && count >= 38);
}

// A guest function has at most one REX_HOOK_RAW replacement in the project.
void TestRawHooksAreUnique() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::regex hook(R"(REX_HOOK_RAW\((sub_[0-9A-Fa-f]+)\))");
  std::set<std::string> seen;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "src")) {
    const std::string ext = entry.path().extension().string();
    if (ext != ".cpp" && ext != ".h") continue;
    const std::string text = check::ReadText(entry.path());
    for (auto it = std::sregex_iterator(text.begin(), text.end(), hook); it != std::sregex_iterator(); ++it) {
      CHECK(seen.insert((*it)[1].str()).second);
    }
  }
  CHECK(seen.count("sub_823263D8") == 1);
  CHECK(seen.count("sub_82287130") == 1);
}

void TestFrameTimerNeverPublishesZero() {
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  const std::string pacing = check::ReadText(root / "src/frame_pacing.cpp");
  CHECK(pacing.find("REX_HOOK_RAW(sub_823263D8)") != std::string::npos);
  CHECK(pacing.find("kMeasuredFrameMsAddress = 0x82D99128") != std::string::npos);
  CHECK(pacing.find("crackdown::TakeFrameMs(g_frame_ms_fraction)") != std::string::npos);
  CHECK(pacing.find("crackdown::TakeWhole(g_frame_ms_fraction)") == std::string::npos);
}

// Every vector multiply-add and dot product the codegen emits goes through the single-rounding helpers, and a build
// without -mfma still runs on CPUs without FMA3.
void TestVectorMultiplyAddIsFused() {
  const std::string patch = ReadPatch("0095-");
  CHECK(!patch.empty());
  CHECK(patch.find("diff --git a/src/codegen/builders/vector.cpp") != std::string::npos);
  CHECK(patch.find("diff --git a/resources/templates/codegen/pch_h.inja") != std::string::npos);
  for (const char* helper : {"rex::ppc::simde_mm_vmaddfp(", "rex::ppc::simde_mm_vnmsubfp(",
                             "rex::ppc::simde_mm_vmsum3fp(", "rex::ppc::simde_mm_vmsum4fp("}) {
    CHECK(patch.find(std::string("+      \"\\tsimde_mm_store_ps({}.f32, ") + helper) != std::string::npos);
  }
  for (const char* helper : {"+inline simde__m128 simde_mm_vmaddfp(", "+inline simde__m128 simde_mm_vnmsubfp(",
                             "+inline simde__m128 simde_mm_vmsum3fp(", "+inline simde__m128 simde_mm_vmsum4fp("}) {
    CHECK(patch.find(helper) != std::string::npos);
  }
  CHECK(HasLine(patch, "-      \"\\tsimde_mm_store_ps({}.f32, simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps({}.f32), \""));
  CHECK(HasLine(patch, "-      \"simde_mm_xor_ps(simde_mm_sub_ps(simde_mm_mul_ps(simde_mm_load_ps({}.f32), \""));
  CHECK(HasLine(patch, "-      \"\\tsimde_mm_store_ps({}.f32, simde_mm_dp_ps(simde_mm_load_ps({}.f32), \""));
  CHECK(HasLine(patch, "+inline const bool kHostHasFma = HostHasFma();"));
  CHECK(HasLine(patch, "+    __asm__(\"vfmadd213ps %2, %1, %0\" : \"+x\"(a) : \"x\"(b), \"x\"(c));"));
  CHECK(HasLine(patch, "+  return detail::EmulatedVmaddfp(a, b, c);"));
  const std::filesystem::path root = std::filesystem::path(CRACKDOWN_PATCH_DIR) / "../..";
  CHECK(check::ReadText(root / "cmake/rexglue_pregenerated.cmake").find("-mfma") == std::string::npos);
  const std::filesystem::path source = root / "out/rexglue-sdk-src/src/codegen/builders/vector.cpp";
  if (!std::filesystem::exists(source)) return;
  const std::string text = check::ReadText(source);
  if (text.find("rex::ppc::simde_mm_vmaddfp(") == std::string::npos) return;
  CHECK(text.find("simde_mm_dp_ps(") == std::string::npos);
  CHECK(text.find("simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps({}.f32)") == std::string::npos);
}

void TestXmaStallGuards() {
  const std::string patch = ReadPatch("0096-");
  CHECK(!patch.empty());
  CHECK(patch.find("diff --git a/src/audio/xma_context.cpp") != std::string::npos);
  CHECK(HasLine(patch, "+        current_frame_remaining_subframes_ == 0) {"));
  CHECK(HasLine(patch, "+    data.output_buffer_write_offset = data.output_buffer_read_offset;"));
  CHECK(HasLine(patch, "+  if (remaining_subframe_blocks_in_output_buffer_ == 0 && output_rb.empty()) {"));
  CHECK(HasLine(patch, "-  if (output_rb.empty()) {"));
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
  TestMainMenuQuitHookIsWired();
  TestMainMenuQuitUsesWindowCloseExit();
  TestXamLaunchTitleOwnsPath();
  TestCompareStringNUsesRealLengths();
  TestScissorEdgesCompareSigned();
  TestD3D12TextureSliceSizeInitialised();
  TestUnusedResolveCountersRemoved();
  TestMakePacketType0SetsOneRegister();
  TestUnusedLocalsKeepGuestReads();
  TestMouseLookHooksAreWired();
  TestMouseLookPatchSuppressesStick();
  TestInvalidFetchPatchOnlyReplacesTheWarning();
  TestHighFrameRateHooksAreWired();
  TestMidasmAddressesAreUnique();
  TestRawHooksAreUnique();
  TestFrameTimerNeverPublishesZero();
  TestVectorMultiplyAddIsFused();
  TestXmaStallGuards();
  return check::Finish("sdk_patch_guards_tests");
}
