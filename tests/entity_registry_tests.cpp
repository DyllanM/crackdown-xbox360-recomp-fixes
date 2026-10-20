#include <cstdint>

#include "check.h"
#include "entity_registry.h"

using namespace crackdown::entity_registry;

namespace {

void TestEmpty() {
  uint32_t objects[4] = {};
  const Removal removal = RemoveObject(objects, 0, 0x1234);
  CHECK(!removal.removed);
  CHECK(removal.count == 0);
}

void TestMiddle() {
  uint32_t objects[4] = {0x10, 0x20, 0x30, 0x40};
  const Removal removal = RemoveObject(objects, 4, 0x20);
  CHECK(removal.removed);
  CHECK(removal.index == 1);
  CHECK(removal.moved == 0x40);
  CHECK(removal.count == 3);
  CHECK(objects[0] == 0x10);
  CHECK(objects[1] == 0x40);
  CHECK(objects[2] == 0x30);
  CHECK(objects[3] == 0);
}

void TestFirst() {
  uint32_t objects[4] = {0x10, 0x20, 0x30, 0x40};
  const Removal removal = RemoveObject(objects, 4, 0x10);
  CHECK(removal.removed);
  CHECK(removal.index == 0);
  CHECK(removal.moved == 0x40);
  CHECK(objects[0] == 0x40);
  CHECK(objects[1] == 0x20);
  CHECK(objects[2] == 0x30);
  CHECK(objects[3] == 0);
}

void TestLast() {
  uint32_t objects[4] = {0x10, 0x20, 0x30, 0x40};
  const Removal removal = RemoveObject(objects, 4, 0x40);
  CHECK(removal.removed);
  CHECK(removal.index == 3);
  CHECK(removal.moved == 0);
  CHECK(removal.count == 3);
  CHECK(objects[2] == 0x30);
  CHECK(objects[3] == 0);
}

void TestMissing() {
  uint32_t objects[4] = {0x10, 0x20, 0x30, 0x40};
  const Removal removal = RemoveObject(objects, 4, 0x99);
  CHECK(!removal.removed);
  CHECK(objects[0] == 0x10);
  CHECK(objects[3] == 0x40);
}

void TestRepeatedRemoval() {
  uint32_t objects[4] = {0x10, 0x20, 0x30, 0x40};
  uint32_t count = 4;
  for (uint32_t object : {0x20u, 0x10u, 0x40u, 0x30u}) {
    const Removal removal = RemoveObject(objects, count, object);
    CHECK(removal.removed);
    count = removal.count;
  }
  CHECK(count == 0);
  for (uint32_t value : objects) {
    CHECK(value == 0);
  }
}

}  // namespace

int main() {
  TestEmpty();
  TestMiddle();
  TestFirst();
  TestLast();
  TestMissing();
  TestRepeatedRemoval();
  return check::Finish("entity_registry_tests");
}
