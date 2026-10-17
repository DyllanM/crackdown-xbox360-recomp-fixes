#pragma once

#include <functional>
#include <utility>

#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

namespace crackdown {

// F10 opens and closes the cheat menu. A held key is one press, and the game never sees the key.
class CheatMenuKey final : public rex::ui::WindowInputListener {
 public:
  // Above ReXApp's input listener (z-order 0), so F10 is consumed before the guest sees it.
  static constexpr size_t kZOrder = 1000;

  CheatMenuKey(rex::ui::Window* window, std::function<void()> toggle) : window_(window), toggle_(std::move(toggle)) {
    window_->AddInputListener(this, kZOrder);
  }

  ~CheatMenuKey() override { window_->RemoveInputListener(this); }

  CheatMenuKey(const CheatMenuKey&) = delete;
  CheatMenuKey& operator=(const CheatMenuKey&) = delete;

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (!IsF10(e)) {
      return;
    }
    e.set_handled(true);
    if (!e.prev_state()) {
      toggle_();
    }
  }

  void OnKeyUp(rex::ui::KeyEvent& e) override {
    if (IsF10(e)) {
      e.set_handled(true);
    }
  }

 private:
  bool IsF10(const rex::ui::KeyEvent& e) const {
    return e.virtual_key() == rex::ui::VirtualKey::kF10 && e.target() == window_;
  }

  rex::ui::Window* window_;
  std::function<void()> toggle_;
};

}  // namespace crackdown
