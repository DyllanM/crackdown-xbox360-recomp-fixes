#pragma once

#include <vector>

#include <trg/launcher.h>

namespace rex::ui {
class AchievementIconCache;
}  // namespace rex::ui

namespace crackdown {

struct AchievementEntry;

// The launcher's Achievements page: the totals, a progress bar and one row per achievement. `icons` may be null.
void DrawAchievementsPage(trg::Ui& ui, const std::vector<AchievementEntry>& entries,
                          rex::ui::AchievementIconCache* icons);

}  // namespace crackdown
