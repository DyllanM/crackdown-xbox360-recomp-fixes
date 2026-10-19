#pragma once

namespace crackdown {

// Registers the clean-exit finalizer once. With Detailed logging off, a normal process exit then
// discards this run's log; with it on, the log is kept. Call after logging is initialized.
void InstallSessionLogCleanup();

// Discards this run's log when Detailed logging is off. Idempotent and never deletes a log that
// already contains a CRASH REPORT. Call before a controlled hard exit, and at normal exit through
// atexit.
void FinalizeSessionLog();

}  // namespace crackdown
