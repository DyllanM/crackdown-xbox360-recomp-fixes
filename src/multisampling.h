#pragma once

namespace crackdown {

// The launcher's Multisampling choice: native 2x MSAA, or 4x compatibility (crackdown_msaa_compatibility).
// Neither can turn off multisampling the game itself asks for.
inline bool Native2xMsaaFor(bool compatibility) {
  return !compatibility;
}

}  // namespace crackdown
