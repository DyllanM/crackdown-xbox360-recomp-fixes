#include <map>
#include <set>
#include <string>

#include "button_glyph_plan.h"
#include "check.h"

using namespace crackdown;

namespace {

void TestPromptKeyFromBind() {
  CHECK(PromptKeyFromBind("E,O,LMB", false) == "E");
  CHECK(PromptKeyFromBind("E,O,LMB", true) == "LMB");
  CHECK(PromptKeyFromBind("Q,I,RMB", true) == "RMB");
  CHECK(PromptKeyFromBind(" Shift+Up , W", false) == "Up");
  CHECK(PromptKeyFromBind("", true).empty());
  CHECK(PromptKeyFromBind(",,", false).empty());
}

void TestButtonGlyphKey() {
  const std::map<std::string, std::string> binds = {
      {"keybind_a", "Space"},          {"keybind_x", "R"},           {"keybind_right_trigger", "E,O,LMB"},
      {"keybind_left_trigger", "Q,I,RMB"}, {"keybind_lstick_up", "W"}, {"keybind_lstick_left", "A"},
      {"keybind_rstick_up", "Up"},      {"keybind_dpad_up", "1"},     {"keybind_start", "Escape"}};
  const std::set<std::string, std::less<>> art = {"Space", "R", "LMB", "E", "RMB", "Q", "W", "A", "WASD",
                                                  "Arrows", "MouseMove", "Escape", "1"};
  auto bind = [&](const char* cvar) {
    const auto it = binds.find(cvar);
    return it == binds.end() ? std::string() : it->second;
  };
  auto has = [&](std::string_view name) { return art.find(name) != art.end(); };
  CHECK(ButtonGlyphKey(0, bind, false, has) == "Space");          // <A>
  CHECK(!ButtonGlyphKey(1, bind, false, has));                    // <B>: no bind, controller glyph stays
  CHECK(!ButtonGlyphKey(4, bind, false, has));                    // <LIVE>
  CHECK(ButtonGlyphKey(7, bind, false, has) == "Escape");         // <S>
  CHECK(ButtonGlyphKey(12, bind, true, has) == "LMB");            // <RT> with mouse look
  CHECK(ButtonGlyphKey(12, bind, false, has) == "E");
  CHECK(ButtonGlyphKey(13, bind, false, has) == "WASD");          // <L>
  CHECK(ButtonGlyphKey(16, bind, false, has) == "A");             // <LL>
  CHECK(ButtonGlyphKey(15, bind, false, has) == "WASD");          // <LUL>
  CHECK(ButtonGlyphKey(14, bind, true, has) == "MouseMove");      // <R> with mouse look
  CHECK(ButtonGlyphKey(14, bind, false, has) == "Arrows");
  CHECK(ButtonGlyphKey(30, bind, true, has) == "MouseMove");      // <RU> follows the mouse
  CHECK(ButtonGlyphKey(39, bind, false, has) == "1");             // <D>: no arrow binds, its up key
  CHECK(!ButtonGlyphKey(40, bind, false, has));
  // A bound key without art keeps the controller glyph.
  auto none = [](std::string_view) { return false; };
  CHECK(!ButtonGlyphKey(0, bind, false, none));
}

void TestPackGlyphSlots() {
  const auto slots = PackGlyphSlots({{56, 56}, {56, 56}, {80, 56}}, 200, 1000, 1200, 4);
  CHECK(slots && slots->size() == 3);
  CHECK((*slots)[0].x0 == 4 && (*slots)[0].y0 == 1004);
  CHECK((*slots)[1].x0 == 64 && (*slots)[1].y0 == 1004);
  // The third does not fit after the second (64 + 56 + 4 + 80 + 4 > 200): next row.
  CHECK((*slots)[2].x0 == 4 && (*slots)[2].y0 == 1064);
  for (const GlyphSlot& s : *slots) CHECK(s.x1 <= 200 && s.y1 <= 1200);
  CHECK(!PackGlyphSlots({{56, 56}}, 200, 1180, 1200, 4));   // no room below
  CHECK(!PackGlyphSlots({{300, 56}}, 200, 0, 1200, 4));     // wider than the atlas
  CHECK(PackGlyphSlots({}, 200, 1200, 1200, 4)->empty());
}

}  // namespace

int main() {
  TestPromptKeyFromBind();
  TestButtonGlyphKey();
  TestPackGlyphSlots();
  return check::Finish("button_glyph_plan_tests");
}
