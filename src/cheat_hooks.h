#pragma once

#include <cstdint>

namespace crackdown::cheats {

// Once per frame on the game thread: applies the refills that turning a cheat on asks for.
void RunGameTick(uint8_t* base);

// Whether the last frame had a single-player session with a controlled player, so the cheats can act.
bool Available();

}  // namespace crackdown::cheats
