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
  const std::array expected = {LauncherPage::kPlay,     LauncherPage::kDisplay,      LauncherPage::kGraphics,
                               LauncherPage::kGameplay, LauncherPage::kControls,     LauncherPage::kAchievements,
                               LauncherPage::kAbout};
  CHECK(kLauncherPageOrder == expected);
}

void TestAchievementsBetweenControlsAndAbout() {
  CHECK(IndexOf(LauncherPage::kAchievements) == IndexOf(LauncherPage::kControls) + 1);
  CHECK(IndexOf(LauncherPage::kAbout) == IndexOf(LauncherPage::kAchievements) + 1);
}

}  // namespace

int main() {
  TestPageOrder();
  TestAchievementsBetweenControlsAndAbout();
  return check::Finish("launcher_pages_tests");
}
