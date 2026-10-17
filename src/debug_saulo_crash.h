#pragma once

#include <cstdint>

namespace crackdown {

// Logs the guest function and registers of the first few guest access violations the runtime leaves unhandled. membase is the guest memory base.
void InstallCrashDiagnostics(const uint8_t* membase);

}  // namespace crackdown
