#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace rex::filesystem {
class VirtualFileSystem;
}

namespace crackdown {

// Modern button glyphs. Before the title starts, builds a copy of the language's font (Local\<lang>\Crackdown_Font.abc
// and .dds) with the atlas upscaled 2x, the 40 controller button glyphs replaced from glyphs\button_glyphs.dds next
// to the executable, and keyboard prompts for the current keybinds in the atlas's free rows. The copy is cached in
// <user data>\4D5307DC\glyphs\<lang> and the game's two font paths are pointed at it; the game files stay untouched.
// Any failure leaves the original font in place.
void InstallGlyphOverlay(rex::filesystem::VirtualFileSystem* file_system, const std::filesystem::path& game_root,
                         const std::filesystem::path& user_data_root, std::string_view language_key);

// Game thread, once per frame: while keyboard and mouse is the device in use, the button glyphs show its keys, and
// the controller glyphs come back with a pad.
void UpdateButtonGlyphs(uint8_t* base);

}  // namespace crackdown
