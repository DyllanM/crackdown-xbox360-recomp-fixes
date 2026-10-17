#pragma once

#include <cstdint>
#include <string_view>

namespace crackdown {

inline constexpr char kLanguageSetting[] = "crackdown_language";
inline constexpr char kDefaultLanguage[] = "en-US";
inline constexpr char kLanguageLabel[] = "Language";
inline constexpr char kLanguageHelp[] =
    "The language of the game's text and spoken dialogue. Applied when the game starts.";

// title_index is the position of the locale in the title's own locale table, which names the Local folder.
struct GameLanguage {
  const char* key;
  const char* label;
  uint32_t title_index;
};

// Locales whose text and voice banks are both on the disc.
inline constexpr GameLanguage kGameLanguages[] = {
    {"en-US", "English", 0},
    {"fr-FR", "French", 1},
    {"de-DE", "German", 3},
    {"it-IT", "Italian", 2},
    {"es-ES", "Spanish (Spain)", 4},
    {"es-LA", "Spanish (Latin America)", 5},
    {"cs-CZ", "Czech", 6},
    {"pl-PL", "Polish", 7},
    {"hu-HU", "Hungarian", 8},
    {"ja-JP", "Japanese", 9},
};

// An unknown value selects English.
constexpr const GameLanguage& LanguageFor(std::string_view key) {
  for (const GameLanguage& language : kGameLanguages) {
    if (key == language.key) {
      return language;
    }
  }
  return kGameLanguages[0];
}

}  // namespace crackdown
