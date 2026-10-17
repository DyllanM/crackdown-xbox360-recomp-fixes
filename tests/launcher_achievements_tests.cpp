#include <string>
#include <vector>

#include "check.h"
#include "launcher_achievements.h"

using namespace crackdown;

namespace {

AchievementEntry Entry(uint32_t gamerscore, bool unlocked, std::string earned = "earned text",
                       std::string unachieved = "locked text") {
  AchievementEntry entry;
  entry.info.gamerscore = gamerscore;
  entry.info.description = std::move(earned);
  entry.info.unachieved_description = std::move(unachieved);
  entry.unlocked = unlocked;
  return entry;
}

void TestTotalsCountUnlockedAndGamerscore() {
  const AchievementTotals totals = TotalAchievements({Entry(10, true), Entry(20, false), Entry(30, true)});
  CHECK(totals.total == 3);
  CHECK(totals.unlocked == 2);
  CHECK(totals.gamerscore_total == 60);
  CHECK(totals.gamerscore_earned == 40);
}

void TestEmptyListHasNoTotals() {
  const AchievementTotals totals = TotalAchievements({});
  CHECK(totals.total == 0);
  CHECK(totals.unlocked == 0);
  CHECK(totals.gamerscore_total == 0);
  CHECK(totals.gamerscore_earned == 0);
}

void TestAllLockedEarnsNoGamerscore() {
  const AchievementTotals totals = TotalAchievements({Entry(5, false), Entry(15, false)});
  CHECK(totals.unlocked == 0);
  CHECK(totals.gamerscore_total == 20);
  CHECK(totals.gamerscore_earned == 0);
}

void TestDescriptionFollowsUnlockState() {
  CHECK(AchievementDescription(Entry(10, true)) == "earned text");
  CHECK(AchievementDescription(Entry(10, false)) == "locked text");
}

void TestDescriptionFallsBackWhenLockedTextIsMissing() {
  CHECK(AchievementDescription(Entry(10, false, "earned text", "")) == "earned text");
}

}  // namespace

int main() {
  TestTotalsCountUnlockedAndGamerscore();
  TestEmptyListHasNoTotals();
  TestAllLockedEarnsNoGamerscore();
  TestDescriptionFollowsUnlockState();
  TestDescriptionFallsBackWhenLockedTextIsMissing();
  return check::Finish("launcher_achievements_tests");
}
