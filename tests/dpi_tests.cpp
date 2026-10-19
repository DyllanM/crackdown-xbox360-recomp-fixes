// Per-monitor DPI helpers: scale derivation, logical/physical conversions and window fitting.
// Run: crackdown_dpi_tests

#include <cmath>
#include <cstdio>

#include "dpi.h"

using namespace crackdown;

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

bool Near(float a, float b) {
  return std::fabs(a - b) < 0.0001f;
}

void TestScales() {
  std::printf("display scales\n");
  CHECK(Near(DpiScaleFromDpi(96), 1.0f));
  CHECK(Near(DpiScaleFromDpi(144), 1.5f));
  CHECK(Near(DpiScaleFromDpi(216), 2.25f));
}

void TestWindowFitsTargetMonitor() {
  std::printf("launcher window uses the target monitor DPI\n");
  CHECK((FitWindowToWorkArea(1280, 720, 3840, 2088, 2.25f, 0.9) == DpiWindowSize{1280, 720}));
  CHECK((FitWindowToWorkArea(1280, 720, 1920, 1040, 1.0f, 0.9) == DpiWindowSize{1280, 720}));
  CHECK((FitWindowToWorkArea(1280, 720, 1920, 1040, 1.5f, 0.9) == DpiWindowSize{1152, 624}));
  CHECK((FitWindowToWorkArea(1280, 720, 1920, 1040, 2.25f, 0.9) == DpiWindowSize{768, 416}));
}

void TestInvalidScales() {
  std::printf("invalid DPI and scale fall back to 1\n");
  CHECK(Near(DpiScaleFromDpi(0), 1.0f));
  CHECK(Near(ValidDpiScale(0.0f), 1.0f));
  CHECK(Near(ValidDpiScale(-1.0f), 1.0f));
  CHECK((FitWindowToWorkArea(1280, 720, 1920, 1040, 0.0f, 0.9) == DpiWindowSize{1280, 720}));
}

}  // namespace

int main() {
  TestScales();
  TestWindowFitsTargetMonitor();
  TestInvalidScales();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
