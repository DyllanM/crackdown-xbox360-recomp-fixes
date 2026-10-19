#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

#include <imgui.h>

#include "check.h"
#include "keybinds.h"

using namespace crackdown;

namespace {

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(CRACKDOWN_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

const KeyboardAction* Find(std::string_view key) {
  for (const KeyboardAction& action : kKeyboardActions) {
    if (action.key == key) {
      return &action;
    }
  }
  return nullptr;
}

struct PendingStore : trg::SettingsStore {
  std::map<std::string, std::string, std::less<>> saved;
  std::map<std::string, std::string, std::less<>> pending;

  PendingStore() {
    true_value = "true";
    false_value = "false";
  }

  std::string Get(std::string_view key, std::string_view def = {}) const override {
    const auto it = pending.find(key);
    return it == pending.end() || it->second.empty() ? std::string(def) : it->second;
  }
  void Set(std::string_view key, std::string value) override { pending[std::string(key)] = std::move(value); }
  bool Save() override {
    saved = pending;
    return true;
  }
};

void TestTable() {
  CHECK(kKeyboardActions.size() == 24);
  for (const KeyboardAction& action : kKeyboardActions) {
    CHECK(std::string_view(action.key).starts_with("keybind_"));
    CHECK(action.label[0] != '\0');
    CHECK(action.default_binding[0] != '\0');
    CHECK(KeyboardBindingIsValid(action.default_binding));
  }
  CHECK(!Find("keybind_guide"));
}

void TestMapping() {
  const struct {
    const char* key;
    const char* label;
    const char* default_binding;
  } kExpected[] = {
      {"keybind_a", "A", "Semicolon,Space"},
      {"keybind_b", "B", "Quote,Backspace"},
      {"keybind_x", "X", "L"},
      {"keybind_y", "Y", "P"},
      {"keybind_left_shoulder", "Left bumper", "1"},
      {"keybind_right_shoulder", "Right bumper", "3"},
      {"keybind_left_trigger", "Left trigger", "Q,I,RMB"},
      {"keybind_right_trigger", "Right trigger", "E,O,LMB"},
      {"keybind_lstick_up", "Left stick up", "W"},
      {"keybind_lstick_down", "Left stick down", "S"},
      {"keybind_lstick_left", "Left stick left", "A"},
      {"keybind_lstick_right", "Left stick right", "D"},
      {"keybind_lstick_press", "Left stick press", "F"},
      {"keybind_rstick_up", "Right stick up", "Up"},
      {"keybind_rstick_down", "Right stick down", "Down"},
      {"keybind_rstick_left", "Right stick left", "Left"},
      {"keybind_rstick_right", "Right stick right", "Right"},
      {"keybind_rstick_press", "Right stick press", "K"},
      {"keybind_dpad_up", "D-pad up", "Shift+Up"},
      {"keybind_dpad_down", "D-pad down", "Shift+Down"},
      {"keybind_dpad_left", "D-pad left", "Shift+Left"},
      {"keybind_dpad_right", "D-pad right", "Shift+Right"},
      {"keybind_back", "View / Back", "Z,Tab"},
      {"keybind_start", "Menu / Start", "X,Return"},
  };
  CHECK(sizeof(kExpected) / sizeof(kExpected[0]) == kKeyboardActions.size());
  for (const auto& expected : kExpected) {
    const KeyboardAction* action = Find(expected.key);
    CHECK(action != nullptr);
    if (action) {
      CHECK(std::string_view(action->label) == expected.label);
      CHECK(std::string_view(action->default_binding) == expected.default_binding);
    }
  }
}

void TestCapture() {
  CHECK(KeyboardBindingFor(ImGuiKey_D, false, false, false) == "D");
  CHECK(KeyboardBindingFor(ImGuiKey_Space, false, false, false) == "Space");
  CHECK(KeyboardBindingFor(ImGuiKey_UpArrow, true, false, false) == "Shift+Up");
  CHECK(KeyboardBindingFor(ImGuiKey_A, false, true, false) == "Ctrl+A");
  CHECK(KeyboardBindingFor(ImGuiKey_S, false, false, true) == "Alt+S");
  CHECK(KeyboardBindingFor(ImGuiKey_F5, true, true, true) == "Shift+Ctrl+Alt+F5");
  CHECK(KeyboardBindingFor(ImGuiKey_Keypad0, false, false, false) == "Numpad0");
  CHECK(KeyboardBindingFor(ImGuiKey_Equal, false, false, false) == "Plus");
  CHECK(KeyboardBindingFor(ImGuiKey_Semicolon, false, false, false) == "Semicolon");

  for (ImGuiKey key : {ImGuiKey_A, ImGuiKey_UpArrow, ImGuiKey_Space, ImGuiKey_F1, ImGuiKey_KeypadAdd}) {
    for (bool shift : {false, true}) {
      for (bool ctrl : {false, true}) {
        for (bool alt : {false, true}) {
          CHECK(KeyboardBindingIsValid(KeyboardBindingFor(key, shift, ctrl, alt)));
        }
      }
    }
  }
}

void TestRejection() {
  for (ImGuiKey key : {ImGuiKey_LeftShift, ImGuiKey_RightShift, ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl,
                       ImGuiKey_LeftAlt, ImGuiKey_RightAlt}) {
    CHECK(KeyboardBindingFor(key, false, false, false).empty());
  }
  for (ImGuiKey key : {ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F7, ImGuiKey_F10, ImGuiKey_F11,
                       ImGuiKey_F12, ImGuiKey_Escape, ImGuiKey_GraveAccent}) {
    CHECK(KeyboardBindingIsReserved(key));
    CHECK(KeyboardBindingFor(key, false, false, false).empty());
    CHECK(KeyboardBindingFor(key, true, false, false).empty());
  }
  for (ImGuiKey key : {ImGuiKey_GamepadFaceDown, ImGuiKey_MouseLeft, ImGuiKey_KeypadDecimal, ImGuiKey_Oem102,
                       ImGuiKey_Menu}) {
    CHECK(KeyboardKeyName(key) == nullptr);
    CHECK(KeyboardBindingFor(key, false, false, false).empty());
  }
  CHECK(!KeyboardBindingIsValid("Escape"));
  CHECK(!KeyboardBindingIsValid("F12"));
  CHECK(!KeyboardBindingIsValid("Backtick"));
}

void TestEscapeCancelsCapture() {
  CHECK(KeyboardBindingIsReserved(ImGuiKey_Escape));
  const std::string captured = KeyboardBindingFor(ImGuiKey_Escape, false, false, false);
  CHECK(captured.empty());

  PendingStore store;
  store.Set("keybind_y", "G");
  CHECK(store.Save());
  if (!captured.empty()) {
    store.Set("keybind_y", captured);
  }
  CHECK(SavedKeyboardBinding(store, *Find("keybind_y")) == "G");
}

void TestValidity() {
  for (const char* good : {"Space", "Semicolon,Space", "Shift+Up", "Ctrl+Shift+W", "NumpadPlus", "F1",
                           "Shift+Up,Shift+Down", " Z , Tab ", "Q,I,RMB", "E,O,LMB", "LMB"}) {
    CHECK(KeyboardBindingIsValid(good));
  }
  for (const char* bad : {"", "NotAKey", "Shift", "Shift+", "Foo+Up", "Up,", ",Up", "Up,,Down", "Shift+Escape",
                          "Ctrl+F3", "Control+", "Return+", "MouseMove"}) {
    CHECK(!KeyboardBindingIsValid(bad));
  }
}

void TestLabel() {
  CHECK(KeyboardBindingLabel("Semicolon,Space") == "Semicolon or Space");
  CHECK(KeyboardBindingLabel("Shift+Up") == "Shift+Up");
  CHECK(KeyboardBindingLabel("Z,Tab") == "Z or Tab");
  CHECK(KeyboardBindingLabel("Q,I,RMB") == "Q or I or RMB");
  CHECK(KeyboardBindingLabel("") == "None");
}

void TestSaved() {
  PendingStore store;
  for (const KeyboardAction& action : kKeyboardActions) {
    CHECK(SavedKeyboardBinding(store, action) == action.default_binding);
  }
  store.Set("keybind_a", "Shift+Up");
  CHECK(SavedKeyboardBinding(store, *Find("keybind_a")) == "Shift+Up");
  CHECK(KeyboardBindingLabel(SavedKeyboardBinding(store, *Find("keybind_a"))) == "Shift+Up");
  store.Set("keybind_b", "NotAKey");
  CHECK(SavedKeyboardBinding(store, *Find("keybind_b")) == "Quote,Backspace");
  store.Set("keybind_start", "F12");
  CHECK(SavedKeyboardBinding(store, *Find("keybind_start")) == "X,Return");
  CHECK(KeyboardBindingLabel(SavedKeyboardBinding(store, *Find("keybind_back"))) == "Z or Tab");
  CHECK(SavedKeyboardBinding(store, *Find("keybind_right_trigger")) == "E,O,LMB");
}

void TestPendingAndReset() {
  PendingStore store;
  for (const KeyboardAction& action : kKeyboardActions) {
    store.pending[action.key] = action.default_binding;
    store.saved[action.key] = action.default_binding;
  }
  store.saved["crackdown_show_fps"] = "true";
  store.pending["crackdown_show_fps"] = "true";

  store.Set("keybind_a", "G");
  CHECK(SavedKeyboardBinding(store, *Find("keybind_a")) == "G");
  CHECK(store.saved["keybind_a"] == "Semicolon,Space");
  CHECK(store.Save());
  CHECK(store.saved["keybind_a"] == "G");

  ResetKeyboardDefaults(store);
  for (const KeyboardAction& action : kKeyboardActions) {
    CHECK(store.Get(action.key) == action.default_binding);
    CHECK(SavedKeyboardBinding(store, action) == action.default_binding);
  }
  CHECK(store.Get("crackdown_show_fps") == "true");
  CHECK(store.saved["keybind_a"] == "G");
  CHECK(store.Save());
  CHECK(store.saved["keybind_a"] == "Semicolon,Space");
  CHECK(store.Get("keybind_right_trigger") == "E,O,LMB");
  CHECK(store.Get("keybind_left_trigger") == "Q,I,RMB");
}

void TestSerialization() {
  const auto safe = [](std::string_view value) {
    return value.find('"') == std::string_view::npos && value.find('\\') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos && value.find('\r') == std::string_view::npos;
  };
  for (const KeyboardAction& action : kKeyboardActions) {
    CHECK(safe(action.key));
    CHECK(safe(action.default_binding));
  }
  for (ImGuiKey key : {ImGuiKey_A, ImGuiKey_Apostrophe, ImGuiKey_Backslash, ImGuiKey_UpArrow, ImGuiKey_KeypadAdd}) {
    CHECK(safe(KeyboardBindingFor(key, true, true, true)));
  }
}

void TestSources() {
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "#include \"keybinds.h\""));
  CHECK(Has(settings, "for (const KeyboardAction& action : kKeyboardActions)"));
  CHECK(Has(settings, "const std::vector<std::string> kGameSettingKeys"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(key, key == \"mnk_sensitivity\""));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.KeyBindRow(action.label, nullptr, shown)"));
  CHECK(Has(launcher, "ResetKeyboardDefaults(ui.settings())"));
  CHECK(Has(launcher, "KeyboardBindingFor(*key, io.KeyShift, io.KeyCtrl, io.KeyAlt)"));
  CHECK(Has(launcher, "KeyboardBindingLabel(SavedKeyboardBinding(ui.settings(), action))"));
  CHECK(Has(launcher, "Click a key, then press the new one. Esc cancels."));
  CHECK(Has(launcher, "\"Keyboard defaults\""));
  CHECK(Has(launcher, "ui.Toggle(\"Keyboard\","));
  CHECK(Has(launcher, "\"mnk_mode\", false,"));
  CHECK(Has(launcher, "\"Off\", \"On\""));
  CHECK(Has(launcher, "ui.settings().GetBool(\"mnk_mode\", false)"));
  CHECK(Has(launcher, "ImGui::BeginDisabled()"));

  const std::string app = ReadSource("crackdown_app.h");
  CHECK(Has(app, "SetDeviceAssignment(std::make_unique<rex::input::SharedAssignment>())"));
}

void TestKeyboardToggle() {
  const std::string config = check::ReadText(std::string(CRACKDOWN_SRC_DIR) + "/../config/crackdown.toml");
  CHECK(Has(config, "mnk_mouse = true"));
  CHECK(Has(config, "keybind_right_trigger = \"E,O,LMB\""));
  CHECK(Has(config, "keybind_left_trigger = \"Q,I,RMB\""));

  PendingStore store;
  CHECK(store.Get("mnk_mode", "false") == "false");
  store.SetBool("mnk_mode", true);
  CHECK(store.Get("mnk_mode") == "true");
  CHECK(store.Save());
  CHECK(store.saved["mnk_mode"] == "true");

  store.Set("keybind_a", "G");
  store.Set("mnk_mode", "false");
  CHECK(store.Save());
  ResetKeyboardDefaults(store);
  CHECK(store.Get("mnk_mode") == "false");
  CHECK(store.Get("keybind_a") == "Semicolon,Space");

  store.Set("keybind_b", "H");
  store.Set("mnk_mode", "false");
  CHECK(store.Save());
  PendingStore reopened;
  reopened.saved = store.saved;
  reopened.pending = store.saved;
  CHECK(reopened.Get("mnk_mode") == "false");
  CHECK(SavedKeyboardBinding(reopened, *Find("keybind_b")) == "H");
  reopened.Set("mnk_mode", "true");
  CHECK(reopened.Save());
  CHECK(SavedKeyboardBinding(reopened, *Find("keybind_b")) == "H");
}

void TestHotkeySeparation() {
  CHECK(Has(ReadSource("frame_counter.cpp"), "RegisterBind(\"bind_crackdown_frame_counter\", \"F2\""));
  CHECK(Has(ReadSource("cheat_menu_key.h"), "VirtualKey::kF10"));
  CHECK(Has(ReadSource("window_host.cpp"), "VirtualKey::kF11"));
  CHECK(Has(ReadSource("crackdown_app.h"), "CreateAchievementsOverlay"));
  for (const char* file : {"frame_counter.cpp", "cheat_menu_key.h", "achievements_overlay.cpp", "crackdown_app.h"}) {
    CHECK(!Has(ReadSource(file), "mnk_mode"));
  }
}

}  // namespace

int main() {
  TestTable();
  TestMapping();
  TestCapture();
  TestRejection();
  TestEscapeCancelsCapture();
  TestValidity();
  TestLabel();
  TestSaved();
  TestPendingAndReset();
  TestSerialization();
  TestSources();
  TestKeyboardToggle();
  TestHotkeySeparation();
  return check::Finish("keyboard_tests");
}
