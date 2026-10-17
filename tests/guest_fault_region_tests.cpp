#include <cstdint>
#include <cstring>

#include <rex/system/guest_fault_region.h>

#include "check.h"

using namespace rex::memory;

namespace {

constexpr uint32_t kPage = 0x10000;
constexpr GuestStackBounds kStack = {0x706C0000, 0x70700000};

GuestFaultRegion Classify(uint32_t page, bool reserved, bool committed, bool no_access,
                          GuestStackBounds stack = kStack) {
  return ClassifyGuestFaultRegion(page, kPage, reserved, committed, no_access, stack);
}

void TestOwnStack() {
  CHECK(Classify(0x706C0000, true, true, false) == GuestFaultRegion::kOwnStack);
  CHECK(Classify(0x706F0000, true, true, false) == GuestFaultRegion::kOwnStack);
  CHECK(Classify(0x70700000, true, true, true) == GuestFaultRegion::kOwnStackGuard);
  CHECK(Classify(0x706B0000, true, true, true) == GuestFaultRegion::kOwnStackGuard);
  CHECK(Classify(0x70710000, false, false, false) == GuestFaultRegion::kOwnStackAbove);
  CHECK(Classify(0x70820000, true, true, true, {0x707E0000, 0x70820000}) ==
        GuestFaultRegion::kOwnStackGuard);
}

void TestOtherMemory() {
  CHECK(Classify(0x70720000, true, true, true) == GuestFaultRegion::kNoAccess);
  CHECK(Classify(0x70720000, true, true, false) == GuestFaultRegion::kCommitted);
  CHECK(Classify(0x70720000, true, false, false) == GuestFaultRegion::kReserved);
  CHECK(Classify(0x70720000, false, false, false) == GuestFaultRegion::kFree);
  CHECK(Classify(0x706A0000, false, false, false) == GuestFaultRegion::kFree);
}

void TestUnknownStack() {
  CHECK(!GuestStackBounds{}.known());
  CHECK(Classify(0x70700000, true, true, true, {}) == GuestFaultRegion::kNoAccess);
  CHECK(Classify(0x70700000, false, false, false, {}) == GuestFaultRegion::kFree);
}

void TestNames() {
  CHECK(std::strcmp(GuestFaultRegionName(GuestFaultRegion::kOwnStackGuard), "own_stack_guard") == 0);
  CHECK(std::strcmp(GuestFaultRegionName(GuestFaultRegion::kOwnStackAbove), "own_stack_above") == 0);
  CHECK(std::strcmp(GuestFaultRegionName(GuestFaultRegion::kNoAccess), "no_access") == 0);
  CHECK(std::strcmp(GuestFaultRegionName(GuestFaultRegion::kFree), "free") == 0);
}

}  // namespace

int main() {
  TestOwnStack();
  TestOtherMemory();
  TestUnknownStack();
  TestNames();
  return check::Finish("guest_fault_region_tests");
}
