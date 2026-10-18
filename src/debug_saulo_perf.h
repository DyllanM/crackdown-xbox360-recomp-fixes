#pragma once

// DEBUG_SAULO: temporary performance report, one group of log lines every few seconds. Remove before release.

#include <cstdint>

namespace crackdown::debug_saulo {

int64_t PerfNowNs();

// A swap the title rendered (not the D3D re-present of the last frame).
void RecordGuestFrame();
void RecordRepeatedPresent();

void StartPerfReport();
void StopPerfReport();

// Each subsystem logs its own counters; called from the report thread.
void LogMainLoopStats();
void LogTimingStats();
void LogFenceWaitStats();
void LogGuestAudioStats();

}  // namespace crackdown::debug_saulo
