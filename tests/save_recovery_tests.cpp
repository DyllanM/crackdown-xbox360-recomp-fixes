#include <cstdint>

#include <rex/system/guest_page_recovery.h>

#include "check.h"

using namespace rex::memory;

namespace {

constexpr uint32_t kPage = 0x10000;

uint32_t PageOf(uint32_t address) {
  return address & ~(kPage - 1);
}

GuestPageRecoveryVerdict Evaluate(const GuestPageRecovery& recovery, GuestAccessKind access,
                                  uint32_t address, bool committed = false) {
  return recovery.Evaluate(access, address, PageOf(address), committed);
}

void TestQualifyingReads() {
  GuestPageRecovery recovery;
  for (const uint32_t address : {0x70700008u, 0x70820008u, 0x70000000u, 0x70ABCDEFu, 0x70FFFFFFu}) {
    CHECK(Evaluate(recovery, GuestAccessKind::kRead, address) == GuestPageRecoveryVerdict::kRecover);
  }
}

void TestNonQualifying() {
  GuestPageRecovery recovery;
  CHECK(Evaluate(recovery, GuestAccessKind::kWrite, 0x70700008) == GuestPageRecoveryVerdict::kNotEligible);
  CHECK(Evaluate(recovery, GuestAccessKind::kExecute, 0x70700008) == GuestPageRecoveryVerdict::kNotEligible);
  for (const uint32_t address : {0x6FFFFFFFu, 0x71000000u, 0x00000008u, 0x82A5CD68u, 0xFFFFFFFFu}) {
    CHECK(Evaluate(recovery, GuestAccessKind::kRead, address) == GuestPageRecoveryVerdict::kNotEligible);
  }
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70700008, true) == GuestPageRecoveryVerdict::kNotEligible);
}

void TestUnrelatedFaultLeavesStateAlone() {
  GuestPageRecovery recovery;
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x0FC9B020) == GuestPageRecoveryVerdict::kNotEligible);
  CHECK(recovery.count() == 0);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70700008) == GuestPageRecoveryVerdict::kRecover);
}

void TestRepeatedFault() {
  GuestPageRecovery recovery;
  recovery.Record(PageOf(0x70820008));
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70820008) == GuestPageRecoveryVerdict::kAlreadyRecovered);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70830008) == GuestPageRecoveryVerdict::kRecover);
  CHECK(recovery.count() == 1);
}

void TestLimit() {
  GuestPageRecovery recovery;
  for (uint32_t i = 0; i < GuestPageRecovery::kMaxPages; ++i) {
    const uint32_t address = 0x70000000 + i * kPage;
    CHECK(Evaluate(recovery, GuestAccessKind::kRead, address) == GuestPageRecoveryVerdict::kRecover);
    recovery.Record(PageOf(address));
  }
  CHECK(recovery.count() == 8);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70900008) == GuestPageRecoveryVerdict::kLimitReached);
  CHECK(Evaluate(recovery, GuestAccessKind::kRead, 0x70000008) == GuestPageRecoveryVerdict::kAlreadyRecovered);
}

}  // namespace

int main() {
  TestQualifyingReads();
  TestNonQualifying();
  TestUnrelatedFaultLeavesStateAlone();
  TestRepeatedFault();
  TestLimit();
  return check::Finish("save_recovery_tests");
}
