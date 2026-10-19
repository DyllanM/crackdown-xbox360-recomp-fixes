#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crackdown {

// The game's 40 button glyphs are the font characters U+0190.. in this order; its strings reach them through tokens
// such as <A> or <RT>, replaced when the text is set.
inline constexpr char16_t kFirstButtonGlyph = 0x0190;
inline constexpr uint32_t kButtonGlyphCount = 40;

// The keybind cvar a button token stands for, empty when the token has no single key: the Live, blank and stick or
// D-pad entries, which ButtonGlyphKey resolves from several binds.
inline constexpr std::array<const char*, kButtonGlyphCount> kButtonGlyphBinds = {
    "keybind_a", "keybind_b", "keybind_x", "keybind_y", "", "", "", "keybind_start", "keybind_back",
    "keybind_left_shoulder", "keybind_right_shoulder", "keybind_left_trigger", "keybind_right_trigger",
    // <L> <R>
    "", "",
    // Left stick: UL L DL D DR R UR U
    "", "keybind_lstick_left", "", "keybind_lstick_down", "", "keybind_lstick_right", "", "keybind_lstick_up",
    // Right stick
    "", "keybind_rstick_left", "", "keybind_rstick_down", "", "keybind_rstick_right", "", "keybind_rstick_up",
    // D-pad
    "", "keybind_dpad_left", "", "keybind_dpad_down", "", "keybind_dpad_right", "", "keybind_dpad_up",
    // <D>
    ""};

// A bind lists alternatives ("E,O,LMB") and may combine keys ("Shift+Up"). The prompt shows one key: a mouse button
// when the bind has one and mouse play is on, otherwise the first alternative, and the last key of a combination.
inline std::string PromptKeyFromBind(std::string_view bind, bool prefer_mouse) {
  std::vector<std::string> alternatives;
  size_t start = 0;
  while (start <= bind.size()) {
    const size_t comma = bind.find(',', start);
    const size_t end = comma == std::string_view::npos ? bind.size() : comma;
    std::string_view part = bind.substr(start, end - start);
    while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
    while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
    if (const size_t plus = part.rfind('+'); plus != std::string_view::npos && plus + 1 < part.size()) {
      part.remove_prefix(plus + 1);
    }
    if (!part.empty()) {
      alternatives.emplace_back(part);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  if (alternatives.empty()) {
    return {};
  }
  if (prefer_mouse) {
    for (const std::string& key : alternatives) {
      if (key == "LMB" || key == "RMB" || key == "MMB") {
        return key;
      }
    }
  }
  return alternatives.front();
}

// The keyboard prompt shown in place of button glyph `index`, or nothing to keep the controller glyph. bind reads a
// keybind cvar; has_key says whether the glyph sheet has art for a name.
inline std::optional<std::string> ButtonGlyphKey(uint32_t index,
                                                 const std::function<std::string(const char*)>& bind, bool mouse_look,
                                                 const std::function<bool(std::string_view)>& has_key) {
  auto from = [&](const char* cvar) { return PromptKeyFromBind(bind(cvar), mouse_look); };
  // A whole stick or D-pad: the four-key cap that matches its binds, else its up key.
  auto cluster = [&](const char* up) -> std::string {
    const std::string key = from(up);
    if (key == "W") return "WASD";
    if (key == "Up") return "Arrows";
    return key;
  };
  std::string key;
  if (index >= kButtonGlyphCount) {
    return std::nullopt;
  }
  if (*kButtonGlyphBinds[index]) {
    key = from(kButtonGlyphBinds[index]);
  } else if (index == 13 || (index >= 15 && index <= 22)) {
    key = cluster("keybind_lstick_up");  // <L> and the left stick's diagonals
  } else if (index == 14 || (index >= 23 && index <= 30)) {
    key = mouse_look ? "MouseMove" : cluster("keybind_rstick_up");
  } else if (index >= 31 && index <= 39) {
    key = cluster("keybind_dpad_up");
  }
  // Camera directions follow the mouse while it looks around.
  if (mouse_look && index >= 23 && index <= 30) {
    key = "MouseMove";
  }
  if (key.empty() || !has_key(key)) {
    return std::nullopt;
  }
  return key;
}

struct GlyphSlot {
  uint32_t x0, y0, x1, y1;
};

// Places one slot per size, left to right in rows, inside [0, width) x [top, height) with a gap between slots so
// filtering never bleeds between neighbours. Nothing when they do not all fit.
inline std::optional<std::vector<GlyphSlot>> PackGlyphSlots(const std::vector<std::pair<uint32_t, uint32_t>>& sizes,
                                                            uint32_t width, uint32_t top, uint32_t height,
                                                            uint32_t gap) {
  std::vector<GlyphSlot> slots;
  uint32_t x = gap;
  uint32_t y = top + gap;
  uint32_t row_height = 0;
  for (const auto& [w, h] : sizes) {
    if (w + 2 * gap > width) {
      return std::nullopt;
    }
    if (x + w + gap > width) {
      x = gap;
      y += row_height + gap;
      row_height = 0;
    }
    if (y + h + gap > height) {
      return std::nullopt;
    }
    slots.push_back({x, y, x + w, y + h});
    x += w + gap;
    row_height = std::max(row_height, h);
  }
  return slots;
}

}  // namespace crackdown
