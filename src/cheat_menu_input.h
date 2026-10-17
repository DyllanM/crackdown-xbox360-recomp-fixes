#pragma once

#include <cstdint>

namespace crackdown::cheats {

// One guest controller read, on the game thread: raises the controller events and blanks the pad while the menu is up.
void FilterMenuInput(uint8_t* base, uint32_t user, uint32_t state);

// While true the game reads a neutral pad, and the controller drives the menu instead.
void SetMenuInputOpen(bool open);

// The controller events (NavEvent flags) read since the last call, one per call; 0 when none.
uint8_t TakeMenuNavEvent();

// Whether any pad button or left stick direction is held.
bool MenuInputHeld();

}  // namespace crackdown::cheats
