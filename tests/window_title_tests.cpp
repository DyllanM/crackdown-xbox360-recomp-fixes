#include <optional>

#include "check.h"
#include "window_title.h"

using namespace crackdown;

namespace {

constexpr std::string_view kGame = "Crackdown Recompiled";

void TestSettingCombinations() {
  const WindowSize picture{3360, 1440};
  CHECK(ComposeWindowTitle(kGame, std::nullopt, std::nullopt) == "Crackdown Recompiled");
  CHECK(ComposeWindowTitle(kGame, picture, std::nullopt) == "Crackdown Recompiled | 3360x1440");
  CHECK(ComposeWindowTitle(kGame, std::nullopt, 60.0) == "Crackdown Recompiled | FPS: 60.00");
  CHECK(ComposeWindowTitle(kGame, picture, 60.0) == "Crackdown Recompiled | 3360x1440 | FPS: 60.00");
}

void TestFrameRateFormat() {
  CHECK(ComposeWindowTitle(kGame, std::nullopt, 59.999) == "Crackdown Recompiled | FPS: 60.00");
  CHECK(ComposeWindowTitle(kGame, std::nullopt, 29.5) == "Crackdown Recompiled | FPS: 29.50");
  CHECK(ComposeWindowTitle(kGame, std::nullopt, 0.0) == "Crackdown Recompiled | FPS: 0.00");
}

}  // namespace

int main() {
  TestSettingCombinations();
  TestFrameRateFormat();
  return check::Finish("window_title_tests");
}
