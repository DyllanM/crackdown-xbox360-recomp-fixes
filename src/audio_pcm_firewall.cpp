#include "audio_pcm_firewall.h"

#include <mutex>

#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace {

std::mutex g_mutex;
crackdown::PcmFirewallCounts g_counts;

}  // namespace

namespace crackdown {

PcmFirewallCounts AudioPcmFirewallCounts() {
  std::lock_guard lock(g_mutex);
  return g_counts;
}

}  // namespace crackdown

// Codegen midasm hook before the XAudioSubmitRenderDriverFrame call in sub_82A4BEF8 (config/overrides.toml).
void crackdown_audio_pcm_firewall(PPCRegister& samples) {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  uint8_t* frame = rex::memory::GuestPtr<uint8_t*>(base, samples.u32);
  std::lock_guard lock(g_mutex);
  const uint64_t invalid_before = g_counts.sanitized_nan + g_counts.sanitized_inf;
  crackdown::SanitizeBigEndianPcm(frame, crackdown::kPcmFirewallSamples, g_counts);
  if (!invalid_before && g_counts.sanitized_nan + g_counts.sanitized_inf)
    REXLOG_INFO("[DEBUG_SAULO][AudioSanitize] first invalid PCM frame sanitized");
}
