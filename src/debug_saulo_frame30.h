#pragma once

#include <cstdint>

// DEBUG_SAULO: appends frame pipeline samples to C:\decomp\saulo-frame30.log.
void StartDebugSauloFrame30();
void StopDebugSauloFrame30();
void DebugSauloFrame30GameplayStarted();
void DebugSauloFrame30OnVdSwap();
void DebugSauloFrame30OnSwapSchedule(uint32_t swap_word);
void DebugSauloFrame30OnDelta(float delta);
void DebugSauloFrame30SetTargetFps(int fps);
