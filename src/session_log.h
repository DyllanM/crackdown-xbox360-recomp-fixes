#pragma once

#include <filesystem>
#include <optional>

namespace crackdown {

// Registers the clean-exit finalizer once. With Detailed logging off, a normal process exit then
// discards this run's log; with it on, the log is kept. Call after logging is initialized.
void InstallSessionLogCleanup();

// Discards this run's log when Detailed logging is off. Idempotent and never deletes a log that
// already contains a CRASH REPORT. Call before a controlled hard exit, and at normal exit through
// atexit.
void FinalizeSessionLog();

// The launcher and the game share one normal log. A relaunch hands the active log to the next
// process: Begin sets the inherited path, Cancel clears it when the relaunch fails, and the child
// takes it once the previous process has exited.
void BeginSessionLogHandoff();
void CancelSessionLogHandoff();
std::optional<std::filesystem::path> TakeInheritedSessionLog();

}  // namespace crackdown
