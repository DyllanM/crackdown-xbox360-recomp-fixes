#pragma once

#include <functional>

namespace crackdown::main_menu_quit {

// Called before the title starts. The handler runs after the native confirmation returns Yes.
void SetQuitHandler(std::function<void()> handler);

void Install();

}  // namespace crackdown::main_menu_quit
