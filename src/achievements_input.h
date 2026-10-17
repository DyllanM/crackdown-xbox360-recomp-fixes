#pragma once

#include <functional>

namespace crackdown {

// Called on a guest thread when B closes the achievements overlay. Set before the game starts.
void SetAchievementsCloseRequest(std::function<void()> request);

// While true the game reads a neutral pad, and the B that closes the overlay never reaches it.
void SetAchievementsModal(bool modal);

// Left stick or D-pad direction read from the game's controller while the overlay is up: -1 up .. 1 down.
float AchievementsScrollAxis();

}  // namespace crackdown
