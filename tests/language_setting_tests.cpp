#include <array>
#include <set>
#include <string>
#include <string_view>

#include "check.h"
#include "game_manifest.h"
#include "language_setting.h"
#include "settings_file.h"

using namespace crackdown;

namespace {

bool OnDisc(std::string_view folder) {
  const std::string prefix = "Local/" + std::string(folder) + "/";
  for (const manifest::RequiredFile& file : manifest::kRequiredFiles) {
    if (std::string_view(file.path).starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

void TestDefaultIsEnglishAtTitleIndexZero() {
  CHECK(std::string_view(kDefaultLanguage) == "en-US");
  CHECK(LanguageFor(kDefaultLanguage).title_index == 0);
  CHECK(std::string_view(kGameLanguages[0].key) == kDefaultLanguage);
  CHECK(std::string_view(kLanguageSetting) == "crackdown_language");
}

void TestInvalidValuesSelectEnglish() {
  for (std::string_view value : {"", "xx-XX", "fr-fr", "ru-RU", "pt-BR", "french", " fr-FR", "fr-FR\n"}) {
    CHECK(std::string_view(LanguageFor(value).key) == "en-US");
  }
}

void TestLanguagesAreTheDiscLocalesWithVoices() {
  std::set<std::string> keys;
  std::set<uint32_t> indexes;
  for (const GameLanguage& language : kGameLanguages) {
    CHECK(keys.insert(language.key).second);
    CHECK(indexes.insert(language.title_index).second);
    CHECK(OnDisc(language.key));
    CHECK(LanguageFor(language.key).title_index == language.title_index);
  }
  CHECK(keys.size() == 10);
}

void TestMappingToTitleLocaleTable() {
  // Order of the title's locale table: en-US fr-FR it-IT de-DE es-ES es-LA cs-CZ pl-PL hu-HU ja-JP.
  constexpr std::array<std::string_view, 10> table = {"en-US", "fr-FR", "it-IT", "de-DE", "es-ES",
                                                      "es-LA", "cs-CZ", "pl-PL", "hu-HU", "ja-JP"};
  for (uint32_t index = 0; index < table.size(); ++index) {
    CHECK(LanguageFor(table[index]).title_index == index);
  }
}

void TestLabels() {
  CHECK(std::string_view(LanguageFor("fr-FR").label) == "French");
  CHECK(std::string_view(LanguageFor("es-LA").label) == "Spanish (Latin America)");
  CHECK(std::string_view(LanguageFor("ja-JP").label) == "Japanese");
}

void TestSettingsFileWithAndWithoutTheKey() {
  check::TempDir dir;
  const auto file = dir.path() / "settings.toml";

  const std::string old_file = "window_width = 1280\nfullscreen = true\n";
  check::WriteText(file, old_file);
  CHECK(CheckPlayerSettings(file).state == SettingsFileState::kValid);
  CHECK(check::ReadText(file) == old_file);

  const std::string with_key = old_file + "crackdown_language = \"fr-FR\"\n";
  check::WriteText(file, with_key);
  CHECK(CheckPlayerSettings(file).state == SettingsFileState::kValid);
  CHECK(check::ReadText(file) == with_key);

  const std::string bare = old_file + "crackdown_language = fr-FR\n";
  check::WriteText(file, bare);
  const auto result = CheckPlayerSettings(file);
  CHECK(result.state == SettingsFileState::kRepaired);
  CHECK(check::ReadText(file) == old_file);
}

}  // namespace

int main() {
  TestDefaultIsEnglishAtTitleIndexZero();
  TestInvalidValuesSelectEnglish();
  TestLanguagesAreTheDiscLocalesWithVoices();
  TestMappingToTitleLocaleTable();
  TestLabels();
  TestSettingsFileWithAndWithoutTheKey();
  return check::Finish("language_setting_tests");
}
