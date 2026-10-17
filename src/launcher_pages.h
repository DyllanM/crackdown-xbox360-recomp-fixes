#pragma once

#include <array>

namespace crackdown {

enum class LauncherPage { kPlay, kDisplay, kGraphics, kGameplay, kControls, kAchievements, kAbout };

inline constexpr std::array kLauncherPageOrder = {
    LauncherPage::kPlay,     LauncherPage::kDisplay,      LauncherPage::kGraphics, LauncherPage::kGameplay,
    LauncherPage::kControls, LauncherPage::kAchievements, LauncherPage::kAbout,
};

}  // namespace crackdown
