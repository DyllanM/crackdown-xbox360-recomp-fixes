#include <cstdint>

#include <rex/system/guest_page_recovery.h>

#include "check.h"

using namespace rex::memory;

namespace {

constexpr uint32_t kPage = 0x10000;
constexpr GuestStackBounds kMassiveStack = {0x706C0000, 0x70700000};
constexpr GuestStackBounds kMassiveStackElsewhere = {0x707E0000, 0x70820000};

uint32_t PageOf(uint32_t address) {
  return address & ~(kPage - 1);
}

struct PageState {
  bool reserved = false;
  bool committed = false;
  bool no_access = false;
};

constexpr PageState kFreePage{};
constexpr PageState kReservedPage{true, false, false};
constexpr PageState kGuardPage{true, true, true};
constexpr PageState kCommittedPage{true, true, false};

GuestPageRecoveryVerdict Evaluate(const GuestPageRecovery& recovery, GuestAccessKind access,
                                  uint32_t address, PageState state = kFreePage,
                                  GuestStackBounds stack = kMassiveStack) {
  const uint32_t page = PageOf(address);
  const auto region = ClassifyGuestFaultRegion(page, kPage, state.reserved, state.committed,
                                               state.no_access, stack);
  return recovery.Evaluate(access, address, page, kPage, region, stack.known());
}

void TestGenuineUnmappedPageRecovers() {
  GuestPageRecovery recovery;
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70F00010) == GuestPageRecoveryVerdict::kRecover);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70000000) == GuestPageRecoveryVerdict::kRecover);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70FFFFFF, kReservedPage) ==
        GuestPageRecoveryVerdict::kRecover);
}

void TestAccessKindAndRange() {
  GuestPageRecovery recovery;
  CHECK(Evaluate(recovery, GuestAccessKind::kWrite, 0x70F00010) == GuestPageRecoveryVerdict::kNotEligible);
  CHECK(Evaluate(recovery, GuestAccessKind::kExecute, 0x70F00010) == GuestPageRecoveryVerdict::kNotEligible);
  for (const uint32_t address : {0x6FFFFFFFu, 0x71000000u, 0x00000008u, 0x82A5CD68u, 0xA0000000u, 0xFFFFFFFFu}) {
    CHECK(Evaluate(recovery, GuestAccessKind::kRead, address) == GuestPageRecoveryVerdict::kNotEligible);
  }
}

void TestMappedPagesRefused() {
  GuestPageRecovery recovery;
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70F00010, kCommittedPage) ==
        GuestPageRecoveryVerdict::kNotEligible);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70F00010, kGuardPage) ==
        GuestPageRecoveryVerdict::kNotEligible);
}

void TestStackFaultsRefused() {
  GuestPageRecovery recovery;
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70700008, kGuardPage) ==
        GuestPageRecoveryVerdict::kNotEligible);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70820008, kGuardPage, kMassiveStackElsewhere) ==
        GuestPageRecoveryVerdict::kNotEligible);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, kMassiveStack.base + 8) ==
        GuestPageRecoveryVerdict::kNotEligible);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, kMassiveStack.limit - 8) ==
        GuestPageRecoveryVerdict::kNotEligible);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x706FFF38) == GuestPageRecoveryVerdict::kNotEligible);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, kMassiveStack.base + kPage + 8) ==
        GuestPageRecoveryVerdict::kNotEligible);
  CHECK(recovery.count() == 0);
}

void TestUnknownStackRefused() {
  GuestPageRecovery recovery;
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70F00010, kFreePage, GuestStackBounds{}) ==
        GuestPageRecoveryVerdict::kNotEligible);
}

void TestRepeatedFault() {
  GuestPageRecovery recovery;
  recovery.Record(PageOf(0x70F00010));
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70F00010) == GuestPageRecoveryVerdict::kAlreadyRecovered);
  CHECK(recovery.count() == 1);
}

void TestSequentialRunawayRefused() {
  GuestPageRecovery recovery;
  recovery.Record(0x70F00000);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70F10008) == GuestPageRecoveryVerdict::kSequential);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70EF0008) == GuestPageRecoveryVerdict::kSequential);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70F20008) == GuestPageRecoveryVerdict::kRecover);
}

void TestLimit() {
  GuestPageRecovery recovery;
  for (uint32_t i = 0; i < GuestPageRecovery::kMaxPages; ++i) {
    const uint32_t address = 0x70000000 + i * 2 * kPage;
    CHECK(Evaluate(recovery, GuestAccessKind::kRead, address) == GuestPageRecoveryVerdict::kRecover);
    recovery.Record(PageOf(address));
  }
  CHECK(recovery.count() == 8);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70F00008) == GuestPageRecoveryVerdict::kLimitReached);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70000008) == GuestPageRecoveryVerdict::kAlreadyRecovered);
}

}  // namespace

int main() {
  TestGenuineUnmappedPageRecovers();
  TestAccessKindAndRange();
  TestMappedPagesRefused();
  TestStackFaultsRefused();
  TestUnknownStackRefused();
  TestRepeatedFault();
  TestSequentialRunawayRefused();
  TestLimit();
  return check::Finish("save_recovery_tests");
}
