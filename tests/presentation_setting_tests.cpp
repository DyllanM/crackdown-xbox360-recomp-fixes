#include <string_view>

#include "check.h"
#include "presentation_setting.h"

using namespace crackdown;

namespace {

void TestKeyName() {
  CHECK(std::string_view(kLetterboxSetting) == "present_letterbox");
}

void TestSegmentsRoundTrip() {
  CHECK(LetterboxSegment(true) == 0);
  CHECK(LetterboxSegment(false) == 1);
  CHECK(LetterboxFromSegment(LetterboxSegment(true)));
  CHECK(!LetterboxFromSegment(LetterboxSegment(false)));
}

void TestLabelIsNotAspectRatio() {
  CHECK(std::string_view(kPresentationLabel) == "Presentation");
  CHECK(std::string_view(kPresentationLabel) != "Aspect Ratio");
}

}  // namespace

int main() {
  TestKeyName();
  TestSegmentsRoundTrip();
  TestLabelIsNotAspectRatio();
  return check::Finish("presentation_setting_tests");
}
