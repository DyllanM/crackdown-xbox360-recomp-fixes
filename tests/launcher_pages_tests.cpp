#include <algorithm>
#include <array>

#include "check.h"
#include "launcher_pages.h"

using namespace crackdown;

namespace {

int IndexOf(LauncherPage page) {
  return int(std::find(kLauncherPageOrder.begin(), kLauncherPageOrder.end(), page) - kLauncherPageOrder.begin());
}

void TestPageOrder() {
  const std::array expected = {LauncherPage::kPlay,         LauncherPage::kDisplay,    LauncherPage::kGraphics,
                               LauncherPage::kGameplay,     LauncherPage::kCheats,     LauncherPage::kControls,
                               LauncherPage::kAchievements, LauncherPage::kAbout};
  CHECK(kLauncherPageOrder == expected);
}

void TestCheatsBetweenGameplayAndControls() {
  CHECK(IndexOf(LauncherPage::kCheats) == IndexOf(LauncherPage::kGameplay) + 1);
  CHECK(IndexOf(LauncherPage::kControls) == IndexOf(LauncherPage::kCheats) + 1);
}

void TestAchievementsBetweenControlsAndAbout() {
  CHECK(IndexOf(LauncherPage::kAchievements) == IndexOf(LauncherPage::kControls) + 1);
  CHECK(IndexOf(LauncherPage::kAbout) == IndexOf(LauncherPage::kAchievements) + 1);
}

}  // namespace

int main() {
  TestPageOrder();
  TestCheatsBetweenGameplayAndControls();
  TestAchievementsBetweenControlsAndAbout();
  return check::Finish("launcher_pages_tests");
}
