#pragma once

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

#include "aspect_ratio.h"

namespace crackdown {

// The game, then the render resolution and the frame rate when they are shown.
inline std::string ComposeWindowTitle(std::string_view game, std::optional<WindowSize> render_resolution,
                                      std::optional<double> frame_rate) {
  std::string title(game);
  if (render_resolution) {
    title += " | " + std::to_string(render_resolution->width) + "x" + std::to_string(render_resolution->height);
  }
  if (frame_rate) {
    char text[32];
    const auto end = std::to_chars(text, text + sizeof(text), *frame_rate, std::chars_format::fixed, 2).ptr;
    title += " | FPS: " + std::string(text, end);
  }
  return title;
}

}  // namespace crackdown
