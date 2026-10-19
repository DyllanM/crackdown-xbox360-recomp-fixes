#include "glyph_overlay.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <toml++/toml.hpp>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/vfs.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

#include "button_glyph_plan.h"
#include "font_atlas.h"
#include "font_rerender.h"
#include "guest_memory.h"
#include "sha256.h"

namespace {

namespace fa = crackdown::font_atlas;
using crackdown::guest::Load32;
using crackdown::guest::Plausible;

// Bumped when the generated font changes for the same inputs, so caches rebuild.
constexpr std::string_view kGeneratorVersion = "button-glyphs-3";
constexpr uint32_t kAtlasScale = 2;
constexpr uint32_t kSlotGap = 4;
constexpr std::string_view kMountPath = "\\Device\\CrackdownGlyphs";
constexpr std::string_view kGamePartition = "\\Device\\Harddisk0\\Partition1";
constexpr std::string_view kTitleFolder = "4D5307DC";
constexpr std::string_view kFontFiles[] = {"Crackdown_Font.abc", "Crackdown_Font.dds"};

// The font object singleton (cFontRendering): +68 the char map, +72 the glyph count, +76 the 16-byte glyph records,
// all inside the .abc the game read and parses in place.
constexpr uint32_t kFontObjectGlobal = 0x82D94CBC;
constexpr uint32_t kFontMapOffset = 68;
constexpr uint32_t kFontCountOffset = 72;
constexpr uint32_t kFontRecordsOffset = 76;
constexpr uint32_t kGlyphRecordSize = 16;
// Updates between device checks.
constexpr uint32_t kDeviceCheckInterval = 15;

struct ButtonSlot {
  uint16_t record = 0;
  fa::Rect controller;
  std::optional<fa::Rect> keyboard;
};

struct OverlayState {
  bool active = false;
  uint32_t glyph_count = 0;
  std::vector<ButtonSlot> buttons;
};

OverlayState g_state;

std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  return bool(out);
}

struct GlyphSheet {
  fa::Image image;
  std::array<std::optional<fa::Rect>, crackdown::kButtonGlyphCount> controller;
  std::map<std::string, fa::Rect, std::less<>> keys;
};

fa::Rect CellRect(const toml::table& entry) {
  const uint32_t x = entry["x"].value_or(0u);
  const uint32_t y = entry["y"].value_or(0u);
  return {x, y, x + entry["w"].value_or(0u), y + entry["h"].value_or(0u)};
}

std::optional<GlyphSheet> LoadSheet(const std::vector<uint8_t>& manifest_bytes, const std::vector<uint8_t>& dds) {
  GlyphSheet sheet;
  auto image = fa::ReadDds(dds);
  if (!image) {
    return std::nullopt;
  }
  sheet.image = std::move(*image);
  toml::table manifest;
  try {
    const std::string_view text(reinterpret_cast<const char*>(manifest_bytes.data()), manifest_bytes.size());
    manifest = toml::parse(text);
  } catch (const toml::parse_error&) {
    return std::nullopt;
  }
  auto fits = [&](const fa::Rect& r) {
    return r.x1 > r.x0 && r.y1 > r.y0 && r.x1 <= sheet.image.width && r.y1 <= sheet.image.height;
  };
  if (const toml::array* controller = manifest["controller"].as_array()) {
    for (const toml::node& node : *controller) {
      const toml::table* entry = node.as_table();
      const int64_t c = entry ? (*entry)["char"].value_or(int64_t{0}) : 0;
      const fa::Rect rect = entry ? CellRect(*entry) : fa::Rect{};
      if (c >= crackdown::kFirstButtonGlyph && c < crackdown::kFirstButtonGlyph + crackdown::kButtonGlyphCount &&
          fits(rect)) {
        sheet.controller[size_t(c - crackdown::kFirstButtonGlyph)] = rect;
      }
    }
  }
  if (const toml::array* keys = manifest["key"].as_array()) {
    for (const toml::node& node : *keys) {
      const toml::table* entry = node.as_table();
      const std::optional<std::string> name = entry ? (*entry)["name"].value<std::string>() : std::nullopt;
      const fa::Rect rect = entry ? CellRect(*entry) : fa::Rect{};
      if (name && fits(rect)) {
        sheet.keys.emplace(*name, rect);
      }
    }
  }
  return sheet;
}

std::string BindValue(const char* cvar) {
  return rex::cvar::GetFlagByName(cvar);
}

bool MouseLook() {
  return rex::cvar::GetFlagByName("mnk_mouse") == "true";
}

std::string HexDigest(const crackdown::Sha256Digest& digest) {
  std::string hex;
  for (const uint8_t byte : digest) {
    char text[3];
    std::snprintf(text, sizeof(text), "%02x", byte);
    hex += text;
  }
  return hex;
}

// rects.txt: one line per button glyph: record index, controller rect, keyboard rect or "-".
std::string WriteRects(const std::vector<ButtonSlot>& buttons) {
  std::string text;
  for (const ButtonSlot& b : buttons) {
    text += std::to_string(b.record) + ' ' + std::to_string(b.controller.x0) + ' ' + std::to_string(b.controller.y0) +
            ' ' + std::to_string(b.controller.x1) + ' ' + std::to_string(b.controller.y1);
    if (b.keyboard) {
      text += ' ' + std::to_string(b.keyboard->x0) + ' ' + std::to_string(b.keyboard->y0) + ' ' +
              std::to_string(b.keyboard->x1) + ' ' + std::to_string(b.keyboard->y1);
    }
    text += '\n';
  }
  return text;
}

std::optional<std::vector<ButtonSlot>> ReadRects(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::vector<ButtonSlot> buttons;
  std::string line;
  while (std::getline(in, line)) {
    uint32_t v[9] = {};
    std::istringstream fields(line);
    int read = 0;
    while (read < 9 && fields >> v[read]) {
      ++read;
    }
    if (read != 5 && read != 9) {
      return std::nullopt;
    }
    ButtonSlot slot{uint16_t(v[0]), {v[1], v[2], v[3], v[4]}, std::nullopt};
    if (read == 9) {
      slot.keyboard = fa::Rect{v[5], v[6], v[7], v[8]};
    }
    buttons.push_back(slot);
  }
  if (buttons.size() != crackdown::kButtonGlyphCount) {
    return std::nullopt;
  }
  return buttons;
}

// Copies every file of the game's language folder but the font into the overlay folder, keeping copies whose size and
// time already match, so the redirected folder still has the strings, subtitles and menus.
bool MirrorLanguageFolder(const std::filesystem::path& source, const std::filesystem::path& folder) {
  std::error_code ec;
  for (auto it = std::filesystem::recursive_directory_iterator(source, ec);
       !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    const std::filesystem::path relative = std::filesystem::relative(it->path(), source, ec);
    const std::filesystem::path target = folder / relative;
    if (it->is_directory(ec)) {
      std::filesystem::create_directories(target, ec);
      continue;
    }
    const std::string name = relative.filename().string();
    const bool font = relative.parent_path().empty() &&
                      std::any_of(std::begin(kFontFiles), std::end(kFontFiles),
                                  [&](std::string_view font_file) { return name == font_file; });
    if (font) {
      continue;
    }
    std::error_code size_ec;
    std::error_code time_ec;
    if (std::filesystem::file_size(target, size_ec) == it->file_size(ec) && !size_ec &&
        std::filesystem::last_write_time(target, time_ec) == it->last_write_time(ec) && !time_ec) {
      continue;
    }
    std::filesystem::copy_file(it->path(), target, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) {
      std::filesystem::last_write_time(target, it->last_write_time(ec), ec);
    }
    if (ec) {
      return false;
    }
  }
  return !ec;
}

// Builds the font into folder; the button slots on success.
std::optional<std::vector<ButtonSlot>> BuildFont(const std::vector<uint8_t>& abc, const std::vector<uint8_t>& dds,
                                                 const GlyphSheet& sheet, const std::vector<uint8_t>& text_font,
                                                 uint32_t& glyph_count, const std::filesystem::path& folder) {
  auto metrics = fa::ParseAbc(abc);
  auto atlas = fa::ReadDds(dds);
  if (!metrics || !atlas || !fa::GlyphRectsFit(*metrics, atlas->width, atlas->height)) {
    REXLOG_WARN("Button glyphs: the game's font could not be read");
    return std::nullopt;
  }
  fa::Image image = fa::Upscale(*atlas, kAtlasScale);
  if (image.width == 0 || !fa::ScaleGlyphRects(*metrics, kAtlasScale)) {
    return std::nullopt;
  }
  glyph_count = uint32_t(metrics->glyphs.size());
  // Latin text is redrawn from the vector font at the new size; other atlases keep the interpolated pixels.
  if (atlas->width == fa::kLatinAtlasWidth && atlas->height == fa::kLatinAtlasHeight && !text_font.empty()) {
    const uint32_t drawn = fa::RerenderTextGlyphs(image, *metrics, kAtlasScale, text_font);
    REXLOG_INFO("Button glyphs: redrew {} text glyphs from the system font", drawn);
  }

  std::vector<ButtonSlot> buttons(crackdown::kButtonGlyphCount);
  uint32_t used_bottom = 0;
  for (const fa::GlyphRecord& glyph : metrics->glyphs) {
    used_bottom = std::max<uint32_t>(used_bottom, glyph.y1);
  }
  std::vector<std::pair<uint32_t, uint32_t>> key_sizes;
  std::vector<uint32_t> key_buttons;
  std::vector<std::string> key_names;
  for (uint32_t n = 0; n < crackdown::kButtonGlyphCount; ++n) {
    const char16_t c = char16_t(crackdown::kFirstButtonGlyph + n);
    if (c > metrics->max_char || metrics->map[c] == 0) {
      return std::nullopt;
    }
    ButtonSlot& slot = buttons[n];
    slot.record = metrics->map[c];
    slot.controller = fa::GlyphRect(metrics->glyphs[slot.record]);
    if (sheet.controller[n]) {
      fa::PasteFit(image, slot.controller, sheet.image, *sheet.controller[n]);
    }
    const auto key = crackdown::ButtonGlyphKey(n, BindValue, MouseLook(), [&](std::string_view name) {
      return sheet.keys.find(name) != sheet.keys.end();
    });
    if (key && slot.controller.width() && slot.controller.height()) {
      key_sizes.emplace_back(slot.controller.width(), slot.controller.height());
      key_buttons.push_back(n);
      key_names.push_back(*key);
    }
  }
  if (const auto slots = crackdown::PackGlyphSlots(key_sizes, image.width, used_bottom, image.height, kSlotGap)) {
    for (size_t i = 0; i < slots->size(); ++i) {
      const crackdown::GlyphSlot& s = (*slots)[i];
      const fa::Rect rect{s.x0, s.y0, s.x1, s.y1};
      fa::PasteFit(image, rect, sheet.image, sheet.keys.find(key_names[i])->second);
      buttons[key_buttons[i]].keyboard = rect;
    }
  } else {
    REXLOG_WARN("Button glyphs: no room for keyboard prompts in the font atlas");
  }

  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  if (ec || !WriteFile(folder / kFontFiles[0], fa::WriteAbc(*metrics)) ||
      !WriteFile(folder / kFontFiles[1], fa::WriteDdsBc3(image, true))) {
    REXLOG_WARN("Button glyphs: cannot write {}", folder.string());
    return std::nullopt;
  }
  return buttons;
}

}  // namespace

void crackdown::InstallGlyphOverlay(rex::filesystem::VirtualFileSystem* file_system,
                                    const std::filesystem::path& game_root,
                                    const std::filesystem::path& user_data_root, std::string_view language_key) {
  const std::filesystem::path source = game_root / "Local" / std::string(language_key);
  const std::filesystem::path sheet_folder = rex::filesystem::GetExecutableFolder() / "glyphs";
  const auto abc = ReadFile(source / kFontFiles[0]);
  const auto dds = ReadFile(source / kFontFiles[1]);
  const auto manifest = ReadFile(sheet_folder / "button_glyphs.toml");
  const auto sheet_dds = ReadFile(sheet_folder / "button_glyphs.dds");
  const auto font_path = fa::FindArialCompatibleFont();
  const std::vector<uint8_t> text_font = font_path ? ReadFile(*font_path).value_or(std::vector<uint8_t>{})
                                                   : std::vector<uint8_t>{};
  if (!abc || !dds || !manifest || !sheet_dds) {
    REXLOG_WARN("Button glyphs: original glyphs kept (font or glyph sheet missing)");
    return;
  }

  // The cache is valid for these exact inputs, binds and mouse mode included.
  crackdown::Sha256 hash;
  hash.Update(kGeneratorVersion.data(), kGeneratorVersion.size());
  for (const auto* bytes : {&*abc, &*dds, &*manifest, &*sheet_dds, &text_font}) {
    hash.Update(bytes->data(), bytes->size());
  }
  for (const char* cvar : kButtonGlyphBinds) {
    const std::string value = *cvar ? BindValue(cvar) : std::string();
    hash.Update(value.data(), value.size() + 1);
  }
  for (const char* cvar : {"keybind_lstick_up", "keybind_rstick_up", "keybind_dpad_up", "mnk_mouse"}) {
    const std::string value = BindValue(cvar);
    hash.Update(value.data(), value.size() + 1);
  }
  const std::string stamp = HexDigest(hash.Finish());

  const std::filesystem::path cache_root = user_data_root / std::string(kTitleFolder) / "glyphs";
  const std::filesystem::path folder = cache_root / std::string(language_key);
  std::optional<std::vector<ButtonSlot>> buttons;
  uint32_t glyph_count = 0;
  if (const auto cached = ReadFile(folder / "stamp"); cached && std::string(cached->begin(), cached->end()) == stamp) {
    buttons = ReadRects(folder / "rects.txt");
    if (const auto cached_abc = buttons ? ReadFile(folder / kFontFiles[0]) : std::nullopt) {
      const auto metrics = fa::ParseAbc(*cached_abc);
      glyph_count = metrics ? uint32_t(metrics->glyphs.size()) : 0;
    }
  }
  if (!buttons || !glyph_count) {
    const auto sheet = LoadSheet(*manifest, *sheet_dds);
    buttons = sheet ? BuildFont(*abc, *dds, *sheet, text_font, glyph_count, folder) : std::nullopt;
    const std::string rects = buttons ? WriteRects(*buttons) : std::string();
    if (!buttons || !WriteFile(folder / "rects.txt", std::vector<uint8_t>(rects.begin(), rects.end())) ||
        !WriteFile(folder / "stamp", std::vector<uint8_t>(stamp.begin(), stamp.end()))) {
      REXLOG_WARN("Button glyphs: original glyphs kept (the new font could not be built)");
      return;
    }
    REXLOG_INFO("Button glyphs: built the {} font in {}", language_key, folder.string());
  }

  auto device = std::make_unique<rex::filesystem::HostPathDevice>(kMountPath, cache_root, /*read_only=*/true);
  if (!device->Initialize() || !file_system->RegisterDevice(std::move(device))) {
    REXLOG_WARN("Button glyphs: could not mount {}", cache_root.string());
    return;
  }
  // The VFS only follows links while resolving a file's folder (OpenFile resolves the parent, then looks the name up
  // in it), so the whole language folder is redirected; the overlay folder holds the rest of it as copies.
  if (!MirrorLanguageFolder(source, folder)) {
    REXLOG_WARN("Button glyphs: original glyphs kept (cannot copy the language folder into {})", folder.string());
    return;
  }
  file_system->RegisterSymbolicLink(std::string(kGamePartition) + "\\Local\\" + std::string(language_key),
                                    std::string(kMountPath) + "\\" + std::string(language_key));
  // The game opens game:\Local\<lang>\crackdown_font.dds; say where the VFS sends it.
  const std::string probe = "game:\\Local\\" + std::string(language_key) + "\\crackdown_font.dds";
  if (rex::filesystem::Entry* entry = file_system->ResolvePath(probe)) {
    REXLOG_INFO("Button glyphs: {} resolves to {}", probe, entry->absolute_path());
  } else {
    REXLOG_WARN("Button glyphs: {} does not resolve", probe);
  }
  g_state.active = true;
  g_state.glyph_count = glyph_count;
  g_state.buttons = std::move(*buttons);
  REXLOG_INFO("Button glyphs: modern glyphs for {}", language_key);
}

void crackdown::UpdateButtonGlyphs(uint8_t* base) {
  static uint32_t countdown = 0;
  static bool showing_keyboard = false;
  static uint32_t applied_records = 0;
  if (!g_state.active || (countdown && --countdown)) {
    return;
  }
  countdown = kDeviceCheckInterval;

  // Each outcome of the font check is logged once, so a log tells whether the game is drawing the overlay.
  static std::atomic_flag logged_loaded;
  static std::atomic_flag logged_other;
  const uint32_t font = Load32(base, kFontObjectGlobal);
  if (!Plausible(font)) {
    return;
  }
  if (const uint32_t count = Load32(base, font + kFontCountOffset); count != g_state.glyph_count) {
    if (count && !logged_other.test_and_set()) {
      REXLOG_WARN("Button glyphs: the game's font has {} glyphs, the overlay {}", count, g_state.glyph_count);
    }
    return;
  }
  const uint32_t records = Load32(base, font + kFontRecordsOffset);
  const uint32_t map = Load32(base, font + kFontMapOffset);
  // The map and records sit inside the .abc the game parses in place, at whatever alignment the file gives them.
  auto in_heap = [](uint32_t address) { return address >= crackdown::guest::kMinObjectAddress &&
                                               address < crackdown::guest::kMaxObjectAddress; };
  if (!in_heap(records) || !in_heap(map)) {
    return;
  }
  // Only the font this overlay built: the map must send the first button character to the record it expects.
  const uint8_t* entry = rex::memory::GuestPtr<const uint8_t*>(base, map + 2 * uint32_t(kFirstButtonGlyph));
  if (uint16_t(entry[0] << 8 | entry[1]) != g_state.buttons.front().record) {
    if (!logged_other.test_and_set()) {
      REXLOG_WARN("Button glyphs: the game's font is not the overlay's");
    }
    return;
  }
  // The overlay's .abc carries the 2x rects: the first button's record tells which file the game read.
  const uint8_t* first = rex::memory::GuestPtr<const uint8_t*>(base, records + g_state.buttons.front().record * 16);
  const uint16_t loaded_x1 = uint16_t(first[4] << 8 | first[5]);
  const ButtonSlot& first_slot = g_state.buttons.front();
  const bool overlay_loaded =
      loaded_x1 == first_slot.controller.x1 || (first_slot.keyboard && loaded_x1 == first_slot.keyboard->x1);
  if (!logged_loaded.test_and_set()) {
    REXLOG_INFO("Button glyphs: game font loaded, first button record x1={} (overlay {}): {}", loaded_x1,
                first_slot.controller.x1, overlay_loaded ? "the overlay" : "the original font, glyphs left alone");
  }
  if (!overlay_loaded) {
    return;
  }

  // The device the player used last: keyboard and mouse report full-range capabilities the pads never do.
  bool keyboard = false;
  auto* runtime = rex::system::kernel_state() ? rex::system::kernel_state()->emulator() : nullptr;
  if (auto* input = runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr) {
    rex::input::X_INPUT_CAPABILITIES caps = {};
    if (input->GetCapabilities(0, 0, &caps) == 0) {
      keyboard = uint16_t(caps.gamepad.buttons) == 0xFFFF && int16_t(caps.gamepad.thumb_lx) == 0x7FFF;
    }
  }
  // The font is reloaded (same object, new records) when the title reloads its resources.
  if (keyboard == showing_keyboard && records == applied_records) {
    return;
  }
  if (keyboard != showing_keyboard) {
    REXLOG_INFO("Button glyphs: showing {} prompts", keyboard ? "keyboard and mouse" : "controller");
  }
  showing_keyboard = keyboard;
  applied_records = records;
  for (const ButtonSlot& slot : g_state.buttons) {
    const fa::Rect& rect = keyboard && slot.keyboard ? *slot.keyboard : slot.controller;
    const uint32_t record = records + uint32_t(slot.record) * kGlyphRecordSize;
    const uint16_t values[4] = {uint16_t(rect.x0), uint16_t(rect.y0), uint16_t(rect.x1), uint16_t(rect.y1)};
    for (int i = 0; i < 4; ++i) {
      uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, record + 2 * uint32_t(i));
      p[0] = uint8_t(values[i] >> 8);
      p[1] = uint8_t(values[i]);
    }
  }
}
