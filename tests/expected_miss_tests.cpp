#include <cstdint>

#include <rex/filesystem/expected_miss.h>

#include "check.h"

using rex::X_STATUS;
using rex::filesystem::FileAccess;
using rex::filesystem::IsDeviceRelativeGuestPath;
using rex::filesystem::IsExpectedOpenMiss;

namespace {

constexpr uint32_t kRead = FileAccess::kGenericRead | FileAccess::kFileReadData;

void TestReadProbeMissesAreExpected() {
  CHECK(IsExpectedOpenMiss(true, kRead, X_STATUS_NO_SUCH_FILE));
  CHECK(IsExpectedOpenMiss(true, kRead, X_STATUS_OBJECT_NAME_NOT_FOUND));
  CHECK(IsExpectedOpenMiss(true, 0, X_STATUS_NO_SUCH_FILE));
}

void TestRealFailuresStayVisible() {
  CHECK(!IsExpectedOpenMiss(true, kRead, X_STATUS_ACCESS_DENIED));
  CHECK(!IsExpectedOpenMiss(true, kRead, X_STATUS_OBJECT_NAME_INVALID));
  CHECK(!IsExpectedOpenMiss(true, kRead, X_STATUS_INVALID_PARAMETER));
  CHECK(!IsExpectedOpenMiss(true, kRead, X_STATUS_OBJECT_NAME_COLLISION));
  CHECK(!IsExpectedOpenMiss(true, kRead, X_STATUS_UNSUCCESSFUL));
  CHECK(!IsExpectedOpenMiss(true, kRead, X_STATUS_SUCCESS));
}

void TestWritesAndCreatesStayVisible() {
  for (const uint32_t write : {FileAccess::kGenericWrite, FileAccess::kGenericAll, FileAccess::kFileWriteData,
                               FileAccess::kFileAppendData}) {
    CHECK(!IsExpectedOpenMiss(true, kRead | write, X_STATUS_NO_SUCH_FILE));
  }
  CHECK(!IsExpectedOpenMiss(false, kRead, X_STATUS_NO_SUCH_FILE));
  CHECK(!IsExpectedOpenMiss(false, kRead, X_STATUS_OBJECT_NAME_NOT_FOUND));
}

void TestDeviceRelativePaths() {
  CHECK(IsDeviceRelativeGuestPath(""));
  CHECK(IsDeviceRelativeGuestPath("state"));
  CHECK(IsDeviceRelativeGuestPath("state\\fsm\\idle.lua"));
  CHECK(IsDeviceRelativeGuestPath("LogFiles\\"));
  CHECK(!IsDeviceRelativeGuestPath("game:\\default.xex"));
  CHECK(!IsDeviceRelativeGuestPath("cache:\\xuifrontend\\en-US\\scene.xzp"));
  CHECK(!IsDeviceRelativeGuestPath("cache:"));
  CHECK(!IsDeviceRelativeGuestPath("D:\\media"));
  CHECK(!IsDeviceRelativeGuestPath("\\Device\\Harddisk0\\Partition1"));
}

}  // namespace

int main() {
  TestReadProbeMissesAreExpected();
  TestRealFailuresStayVisible();
  TestWritesAndCreatesStayVisible();
  TestDeviceRelativePaths();
  return check::Finish("expected_miss_tests");
}
