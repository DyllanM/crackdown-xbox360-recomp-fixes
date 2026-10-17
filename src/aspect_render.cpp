#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>

#include <rex/hook.h>
#include <rex/ppc.h>

#include "aspect_ratio.h"
#include "settings.h"

namespace crackdown {
namespace {

// The camera aspect the title's view and projection builders read. Its only writer is the constructor sub_822846A0.
constexpr uint32_t kCameraAspectAddress = 0x82BAA498;

void StoreBEFloat(uint8_t* base, uint32_t addr, float value) {
  const uint32_t bits = std::byteswap(std::bit_cast<uint32_t>(value));
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &bits, sizeof(bits));
}

}  // namespace
}  // namespace crackdown

// Replaces the 16:9 the constructor stores. The title's vertical field of view stays, so a wider format shows more of
// the scene to the sides.
REX_EXTERN(__imp__sub_822846A0);
REX_HOOK_RAW(sub_822846A0) {
  __imp__sub_822846A0(ctx, base);
  const std::optional<double> aspect =
      crackdown::CameraAspectOverride(crackdown::ParseAspectRatio(REXCVAR_GET(crackdown_aspect_ratio)));
  if (aspect) {
    crackdown::StoreBEFloat(base, crackdown::kCameraAspectAddress, float(*aspect));
  }
}
