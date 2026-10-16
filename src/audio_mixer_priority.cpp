#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

REXCVAR_DEFINE_BOOL(native_audio_mixer_priority, true, "Audio",
                    "Give the audio mixer callback thread native audio scheduling priority");

REX_EXTERN(__imp__sub_82A4BEF8);

namespace {

bool RaiseNativeAudioPriority() {
#if defined(_WIN32)
  return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
#else
  sched_param param{};
  param.sched_priority = 10;
  return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
#endif
}

}  // namespace

// The XAudio client callback runs on the audio worker thread. That thread requests scheduling priority once.
REX_HOOK_RAW(sub_82A4BEF8) {
  thread_local bool requested = false;
  if (!requested && REXCVAR_GET(native_audio_mixer_priority)) {
    requested = true;
    if (RaiseNativeAudioPriority()) {
      REXLOG_INFO("native_audio_mixer_priority: raised the audio mixer thread priority");
    } else {
      REXLOG_WARN("native_audio_mixer_priority: native audio scheduling unavailable");
    }
  }
  __imp__sub_82A4BEF8(ctx, base);
}
