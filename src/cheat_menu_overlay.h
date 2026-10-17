#pragma once

#include <bitset>
#include <functional>

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

#include "cheat_menu_rules.h"

namespace crackdown {

// The F10 cheat menu: a narrow panel in the top right corner, driven by the keyboard or the controller.
class CheatMenuOverlay final : public rex::ui::ImGuiDialog {
 public:
  struct Controls {
    std::function<void()> finished;  // the menu is closed and the game may read its input again; called during a frame
  };

  CheatMenuOverlay(rex::ui::ImGuiDrawer* imgui_drawer, Controls controls);

  // Starts closing; the game gets its input back once what was pressed in the menu is released.
  void RequestClose() { close_.Request(); }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void TrackKeys();
  void ReadInput();
  void ActivateSelected();
  void DrawEntry(const cheats::MenuEntry& entry, int row, float scale, const ImVec4& accent, const ImVec4& focus,
                 const ImVec4& dim);
  void DrawPanel(ImGuiIO& io);

  Controls controls_;
  cheats::CloseGate close_;
  std::bitset<ImGuiKey_NamedKey_COUNT> pressed_;
  int selected_ = 0;
  bool focused_ = false;
};

}  // namespace crackdown
