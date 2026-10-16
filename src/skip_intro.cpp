#include "skip_intro.h"

#include <atomic>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(crackdown_skip_intro, false, "Crackdown", "Start directly at the title screen");

namespace {

constexpr uint32_t kGuestLowAddress = 0x10000;
constexpr uint32_t kGuestHighAddress = 0xFFFF0000;
constexpr uint32_t kMovieCallLogLimit = 4;
constexpr uint32_t kXuiSkipLogLimit = 8;
constexpr uint32_t kSplashSkipLogLimit = 8;
constexpr uint32_t kMovieNameLimit = 128;
// Singleton byte read by sub_82624A30: zero selects the disabled-video branch, which finishes the video at once.
constexpr uint32_t kVideosEnabledOffset = 152;
// Sequence object state at +272; the per-frame callback receives the object at +136.
constexpr uint32_t kFrameObjectStateOffset = 136;
constexpr uint32_t kSequenceStateStartPrompt = 10;
constexpr uint32_t kSplashFirstState = 6;
constexpr uint32_t kSplashLastState = 8;
// Global byte at 17674 set while the startup presentation runs; the frontend start branch requires it to be zero.
constexpr uint32_t kPresentationFlagAddress = 0x82DE450A;

std::atomic<uint32_t> g_movie_calls{0};
std::atomic<uint32_t> g_xui_skips{0};
std::atomic<uint32_t> g_splash_skips{0};
std::atomic<bool> g_active_logged{false};
std::atomic<bool> g_start_reached{false};

bool IsGuestAddress(uint32_t addr) {
  return addr >= kGuestLowAddress && addr < kGuestHighAddress;
}

uint32_t ReadBE32(uint8_t* base, uint32_t addr) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(value));
  return std::byteswap(value);
}

uint16_t ReadBE16(uint8_t* base, uint32_t addr) {
  uint16_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(value));
  return std::byteswap(value);
}

uint8_t ReadByte(uint8_t* base, uint32_t addr) {
  return *rex::memory::GuestPtr<const uint8_t*>(base, addr);
}

void WriteByte(uint8_t* base, uint32_t addr, uint8_t value) {
  *rex::memory::GuestPtr<uint8_t*>(base, addr) = value;
}

// Name of a guest string object: length at +20 in UTF-16 units, data pointer at +4.
std::string MovieNameLower(uint8_t* base, uint32_t name_object) {
  if (!IsGuestAddress(name_object)) return {};
  const uint32_t data = ReadBE32(base, name_object + 4);
  const uint32_t length = ReadBE32(base, name_object + 20);
  if (!IsGuestAddress(data) || length == 0 || length > kMovieNameLimit) return {};
  std::string name;
  for (uint32_t i = 0; i < length; ++i) {
    const uint16_t unit = ReadBE16(base, data + 2 * i);
    name += unit < 0x80 ? static_cast<char>(std::tolower(unit)) : '?';
  }
  return name;
}

bool IsPreTitleMovie(uint8_t* base, uint32_t name_object) {
  const std::string name = MovieNameLower(base, name_object);
  for (std::string_view movie : {"msgs.bik", "rtw_logo.bik", "crackdownhd.bik", "maingameintro.bik"}) {
    if (std::string_view(name).ends_with(movie)) return true;
  }
  return false;
}

}  // namespace

void InstallSkipIntro() {
  REXLOG_INFO("[SkipIntro] requested={}", REXCVAR_GET(crackdown_skip_intro) ? 1 : 0);
}

REX_EXTERN(__imp__sub_826A8940);
REX_HOOK_RAW(sub_826A8940) {
  const bool skip = REXCVAR_GET(crackdown_skip_intro) && ctx.r3.u32 <= 1;
  if (g_movie_calls.fetch_add(1) < kMovieCallLogLimit) {
    REXLOG_INFO("[SkipIntro] engine_movie index={} skipped={}", ctx.r3.u32, skip ? 1 : 0);
  }
  if (skip) {
    if (!g_active_logged.exchange(true)) REXLOG_INFO("[SkipIntro] active");
    return;
  }
  __imp__sub_826A8940(ctx, base);
}

REX_EXTERN(__imp__sub_8232EA18);
REX_EXTERN(__imp__sub_82624A30);
REX_HOOK_RAW(sub_82624A30) {
  if (!REXCVAR_GET(crackdown_skip_intro) || !IsPreTitleMovie(base, ctx.r4.u32)) {
    __imp__sub_82624A30(ctx, base);
    return;
  }
  if (g_xui_skips.fetch_add(1) < kXuiSkipLogLimit) {
    REXLOG_INFO("[SkipIntro] xui_skip scene={}", MovieNameLower(base, ctx.r4.u32));
  }

  const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64, r6 = ctx.r6.u64, r7 = ctx.r7.u64;
  const uint64_t lr = ctx.lr;
  __imp__sub_8232EA18(ctx, base);
  const uint32_t singleton = ctx.r3.u32;
  ctx.r3.u64 = r3;
  ctx.r4.u64 = r4;
  ctx.r5.u64 = r5;
  ctx.r6.u64 = r6;
  ctx.r7.u64 = r7;
  ctx.lr = lr;

  if (!IsGuestAddress(singleton)) {
    __imp__sub_82624A30(ctx, base);
    return;
  }
  const uint8_t enabled = ReadByte(base, singleton + kVideosEnabledOffset);
  WriteByte(base, singleton + kVideosEnabledOffset, 0);
  __imp__sub_82624A30(ctx, base);
  WriteByte(base, singleton + kVideosEnabledOffset, enabled);
}

REX_EXTERN(__imp__sub_826544E0);
REX_EXTERN(__imp__sub_82653FC0);
REX_HOOK_RAW(sub_82653FC0) {
  if (!REXCVAR_GET(crackdown_skip_intro) || g_start_reached.load()) {
    __imp__sub_82653FC0(ctx, base);
    return;
  }
  const uint32_t frame_object = ctx.r3.u32;
  const uint32_t state = ReadBE32(base, frame_object + kFrameObjectStateOffset);
  if (state == kSequenceStateStartPrompt && ReadByte(base, kPresentationFlagAddress) == 0) {
    g_start_reached.store(true);
    REXLOG_INFO("[SkipIntro] start_prompt_reached");
  }
  const bool done = ReadByte(base, frame_object + 24) != 0;
  if (done || state < kSplashFirstState || state > kSplashLastState) {
    __imp__sub_82653FC0(ctx, base);
    return;
  }
  if (g_splash_skips.fetch_add(1) < kSplashSkipLogLimit) {
    REXLOG_INFO("[SkipIntro] splash_skip id=sequence_state_{}", state);
  }
  ctx.r3.u64 = frame_object - kFrameObjectStateOffset;
  __imp__sub_826544E0(ctx, base);
}
