#pragma once

#include <cstdint>

namespace crackdown::entity_registry {

struct Removal {
  bool removed = false;
  uint32_t index = 0;
  uint32_t moved = 0;
  uint32_t count = 0;
};

// Removes one object pointer by moving the last entry into its place.
inline Removal RemoveObject(uint32_t* objects, uint32_t count, uint32_t object) {
  Removal removal;
  for (uint32_t i = 0; i < count; ++i) {
    if (objects[i] != object) {
      continue;
    }
    removal.removed = true;
    removal.index = i;
    removal.count = count - 1;
    if (i != count - 1) {
      removal.moved = objects[count - 1];
      objects[i] = removal.moved;
    }
    objects[count - 1] = 0;
    return removal;
  }
  return removal;
}

}  // namespace crackdown::entity_registry
