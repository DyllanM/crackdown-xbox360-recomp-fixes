#include "entity_registry.h"

#include <array>
#include <cstdint>

#include <rex/hook.h>
#include <rex/ppc.h>

#include "guest_memory.h"

REX_EXTERN(__imp__sub_824EC668);

namespace crackdown::entity_registry {
namespace {

constexpr uint32_t kManagerGlobal = 0x82DE3F2C;
constexpr uint32_t kObjectArrayOffset = 4;
constexpr uint32_t kCapacity = 308;  // The inline object array ends at the count field.
constexpr uint32_t kCountOffset = 1236;
constexpr uint32_t kSlotOffset = 256;
constexpr uint32_t kNameChainOffset = 260;
constexpr uint32_t kNoSlot = 0xFFFFFFFF;

}  // namespace

void Unregister(uint8_t* base, uint32_t object) {
  const uint32_t manager = guest::Load32(base, kManagerGlobal);
  if (!manager) {
    return;
  }

  const uint32_t count = guest::Load32(base, manager + kCountOffset);
  if (count > kCapacity) {
    return;
  }

  std::array<uint32_t, kCapacity> objects{};
  for (uint32_t i = 0; i < count; ++i) {
    objects[i] = guest::Load32(base, manager + kObjectArrayOffset + i * 4);
  }

  const Removal removal = RemoveObject(objects.data(), count, object);
  if (!removal.removed) {
    return;
  }

  for (uint32_t i = 0; i < removal.count; ++i) {
    guest::Store32(base, manager + kObjectArrayOffset + i * 4, objects[i]);
  }
  guest::Store32(base, manager + kObjectArrayOffset + removal.count * 4, 0);
  guest::Store32(base, manager + kCountOffset, removal.count);

  guest::Store32(base, object + kSlotOffset, kNoSlot);
  guest::Store32(base, object + kNameChainOffset, kNoSlot);
  if (removal.moved != 0 && removal.moved != object) {
    guest::Store32(base, removal.moved + kSlotOffset, removal.index);
  }
}

}  // namespace crackdown::entity_registry

REX_HOOK_RAW(sub_824EC668) {
  crackdown::entity_registry::Unregister(base, ctx.r3.u32);
  __imp__sub_824EC668(ctx, base);
}
