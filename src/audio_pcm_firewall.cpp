#include "audio_pcm_firewall.h"

#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

// Codegen midasm hook before the XAudioSubmitRenderDriverFrame call in sub_82A4BEF8 (config/overrides.toml).
void crackdown_audio_pcm_firewall(PPCRegister& samples) {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  uint8_t* frame = rex::memory::GuestPtr<uint8_t*>(base, samples.u32);
  crackdown::PcmFirewallCounts counts;
  crackdown::SanitizeBigEndianPcm(frame, crackdown::kPcmFirewallSamples, counts);
}
