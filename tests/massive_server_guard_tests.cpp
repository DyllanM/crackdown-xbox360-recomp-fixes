#include <cstdint>

#include "check.h"
#include "massive_server_guard.h"

using crackdown::ClampMassiveServerCount;

int main() {
  CHECK(ClampMassiveServerCount(0) == 0);
  CHECK(ClampMassiveServerCount(1) == 1);
  CHECK(ClampMassiveServerCount(9) == 9);
  CHECK(ClampMassiveServerCount(10) == 10);
  CHECK(ClampMassiveServerCount(11) == 10);
  CHECK(ClampMassiveServerCount(12) == 10);
  CHECK(ClampMassiveServerCount(0x7FFFFFFF) == 10);
  CHECK(ClampMassiveServerCount(0xFFFFFFFF) == 10);
  return check::Finish("massive_server_guard_tests");
}
