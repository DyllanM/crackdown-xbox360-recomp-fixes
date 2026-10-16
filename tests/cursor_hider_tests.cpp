// Cursor auto-hide state machine with synthetic time.

#include <cstdio>

#include "cursor_hider.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

void TestCursorHider() {
  std::printf("cursor hider\n");
  using crackdown::CursorHider;
  using Clock = CursorHider::Clock;
  const auto at = [](int ms) { return Clock::time_point{} + std::chrono::milliseconds(ms); };
  const std::optional<bool> hide = false, show = true;

  CursorHider overlay;
  CHECK(!overlay.Update(false, true, true, at(0)) && !overlay.Update(false, true, true, at(60000)));
  CursorHider windowed;
  CHECK(!windowed.Update(true, false, true, at(0)) && !windowed.Update(true, false, true, at(60000)));

  CursorHider c;
  CHECK(!c.Update(true, true, true, at(0)) && c.visible());
  CHECK(!c.Update(true, true, true, at(4999)));
  CHECK(c.Update(true, true, true, at(5000)) == hide);
  CHECK(!c.Update(true, true, true, at(5100)) && !c.Update(true, true, true, at(9000)));

  c.MouseMoved(at(6000));
  CHECK(c.Update(true, true, true, at(6000)) == show);
  c.MouseMoved(at(8000));
  CHECK(!c.Update(true, true, true, at(12999)) && c.Update(true, true, true, at(13000)) == hide);

  CHECK(c.Update(true, false, true, at(13100)) == show);
  CHECK(!c.Update(true, true, true, at(20000)) && !c.Update(true, true, true, at(24999)));
  CHECK(c.Update(true, true, true, at(25000)) == hide);

  CHECK(c.Update(true, true, false, at(25100)) == show);
  CHECK(!c.Update(true, true, true, at(30000)) && !c.Update(true, true, true, at(34999)));
  CHECK(c.Update(true, true, true, at(35000)) == hide);

  c.MouseMoved(at(36000));
  CHECK(c.Update(true, true, true, at(36000)) == show);
  CHECK(c.Update(true, true, true, at(41000)) == hide);
  CHECK(c.Update(false, true, true, at(41100)) == show);
  CHECK(!c.Update(false, true, true, at(99000)) && c.visible());
}

}  // namespace

int main() {
  TestCursorHider();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
