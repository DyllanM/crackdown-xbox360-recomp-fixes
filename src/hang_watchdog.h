#pragma once

#include <filesystem>

namespace crackdown {

// Counts a main-loop frame of the title (called from the frame hook).
void NoteGuestFrame();

// Starts a thread that watches the window (native handle) and the title's frames. When the window stops answering
// messages, or the title stops producing frames, for several seconds, it writes the call stack of every thread to
// <logs_folder>/hang_<n>.txt and logs the path. Windows only; a no-op elsewhere.
void StartHangWatchdog(void* native_window, const std::filesystem::path& logs_folder);

}  // namespace crackdown
