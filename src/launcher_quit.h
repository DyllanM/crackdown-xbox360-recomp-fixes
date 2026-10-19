#pragma once

namespace crackdown {

// Escape cancels pop-ups and key capture, so a launcher Quit caused by Escape is ignored.
inline bool LauncherQuitAllowed(bool escape_pressed) { return !escape_pressed; }

}  // namespace crackdown
