#include "../src/runtime_health.h"
#include <rex/logging.h>
#include <rex/audio/conversion.h>
#include <spdlog/sinks/base_sink.h>
#include <cassert>
#include <mutex>
#include <thread>
#include <atomic>
#include <cmath>
#include <limits>

class Capture : public spdlog::sinks::base_sink<std::mutex> {
 public:
  std::vector<std::string> entries;
  size_t Count(std::string_view text) {
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (auto& line : entries) count += line.find(text) != line.npos;
    return count;
  }
 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    entries.emplace_back(msg.payload.data(), msg.payload.size());
  }
  void flush_() override {}
};
std::shared_ptr<Capture> capture;
std::shared_ptr<spdlog::logger> logger;
std::atomic<rex::RuntimeObserver> observer{nullptr};
namespace rex {
LogCategoryId RegisterLogCategory(const char*) { return LogCategoryId(0); }
spdlog::logger* GetLoggerRaw(LogCategoryId) { return logger.get(); }
void FlushLogging() {}
void AddSink(spdlog::sink_ptr sink) { logger->sinks().push_back(sink); }
void RemoveSink(spdlog::sink_ptr sink) { std::erase(logger->sinks(), sink); }
void SetRuntimeObserver(RuntimeObserver sink) { observer = sink; }
void ObserveRuntime(const char* category, const char* event, uint64_t object, uint64_t a,
                    uint64_t b, uint64_t c, uint64_t d, const char* detail) {
  if (auto fn = observer.load()) fn(category,event,object,a,b,c,d,detail,0x82001234,7);
}
}
void Pcm(bool active) { rex::ObserveRuntime("AUDIO","pcm",1,256,active ? 100 : 0,active ? 500000 : 0); }
void Submit() { rex::ObserveRuntime("AUDIO","buffer_submitted",1,0,1); }
void CheckExcludedChannels() {
  float input[24]{}, stereo[8]{}, surround[24]{};
  for (size_t ch=0;ch<6;++ch)
    for (size_t sample=0;sample<4;++sample)
      input[ch*4+sample] = rex::byte_swap(ch == 3 ? std::numeric_limits<float>::quiet_NaN() : 0.1f);
  const rex::audio::StereoFold fold;
  rex::audio::conversion::sequential_6_BE_to_interleaved_2_LE(stereo,input,4,fold,1.0f);
  const float expected = (0.1f + 0.1f * fold.center + 0.1f * fold.surround) * fold.scale;
  for (float sample : stereo) assert(std::isfinite(sample) && std::abs(sample-expected) < 0.000001f);
  rex::audio::SurroundMix mix;
  mix.lfe = 0;
  rex::audio::conversion::sequential_6_BE_to_interleaved_6_LE(surround,input,4,mix,1.0f);
  for (size_t i=0;i<24;++i) assert(surround[i] == (i % 6 == 3 ? 0.0f : 0.1f));
  for (size_t i=12;i<16;++i) input[i] = rex::byte_swap(0.2f);
  auto included = fold;
  included.lfe = 0.5f;
  rex::audio::conversion::sequential_6_BE_to_interleaved_2_LE(stereo,input,4,included,1.0f);
  for (float sample : stereo) assert(std::abs(sample-(expected+0.1f*fold.scale)) < 0.000001f);
}
int main() {
  CheckExcludedChannels();
  capture = std::make_shared<Capture>();
  logger = std::make_shared<spdlog::logger>("core",capture);
  StartRuntimeHealth();
  RuntimeHealthGameplayStarted();
  rex::ObserveRuntime("AUDIO","device_open",1,48000,2,2);
  rex::ObserveRuntime("AUDIO","callback",1,256,0,0,1000);
  Submit(); Pcm(true);
  rex::ObserveRuntime("AUDIO","paused");
  for (int i=0;i<8;++i) { Submit(); Pcm(false); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
  assert(capture->Count("[AudioFault]") == 0);
  rex::ObserveRuntime("AUDIO","resumed");
  rex::ObserveRuntime("AUDIO","callback",1,256,0,0,(uint64_t(1)<<32)|1000);
  for (int i=0;i<8;++i) {
    Submit(); Pcm(false); rex::ObserveRuntime("AUDIO","underrun",1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  assert(capture->Count("[AudioFault]") == 0);
  rex::ObserveRuntime("AUDIO","callback",1,256,0,0,1000);
  for (int i=0;i<8;++i) {
    rex::ObserveRuntime("AUDIO","callback",1,256,0,0,1000);
    Submit(); Pcm(false); std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  assert(capture->Count("[AudioFault] silence_detected") == 1);
  Pcm(true);
  const size_t before_flood = capture->Count("[RuntimeHistory] reason=");
  for (int i=0;i<20000;++i) rex::ObserveRuntime("AUDIO","mixer_pcm",1,768,500000,512);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  assert(capture->Count("[RuntimeHistory] reason=") <= before_flood + 1);
  for (int i=0;i<32;++i) rex::ObserveRuntime("AUDIO","mixer_pcm",1,768,500000,0);
  assert(capture->Count("name=invalid_mixer_pcm transition=resolved") == 1);
  for (int i=0;i<20000;++i) rex::ObserveRuntime("AUDIO","mixer_pcm",1,768,500000,512);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  assert(capture->Count("[RuntimeHistory] reason=") <= before_flood + 1);
  std::vector<std::thread> threads;
  for (int t=0;t<4;++t) threads.emplace_back([] {
    for (int i=0;i<1000;++i) rex::ObserveRuntime("KERNEL","stub",0,0,0,0,0,"test_stub");
  });
  for (auto& t : threads) t.join();
  assert(capture->Count("first=test_stub") == 1);
  rex::ObserveRuntime("CHARACTER","constructed",0x1234);
  for (uint64_t level=0;level<5;++level) {
    rex::ObserveRuntime("CHARACTER","descriptor_words",0x1234,level,uint64_t(7)<<32,0,1);
    rex::ObserveRuntime("CHARACTER","descriptor_words",0x1234,0x100|level,0,0,1);
  }
  rex::ObserveRuntime("VFS","probe_missing",0,0,0,0,0,"game:\\state\\example");
  rex::ObserveRuntime("VFS","probe_found",0,0,0,0,0,"state\\example");
  rex::ObserveRuntime("VFS","open_result",0,0xC0000034,0,0,0,"config\\example");
  rex::ObserveRuntime("VFS","probe_found",0,0,0,0,0,"config\\example");
  rex::ObserveRuntime("RESOURCE","request",99,0,0,0,0,"hud_example");
  rex::ObserveRuntime("VFS","probe_missing",0,0,0,0,0,"hud_example");
  rex::ObserveRuntime("RESOURCE","success",99,0,44,55,0,"hud_example");
  rex::ObserveRuntime("HUD","image_identity",66,44,55,0x12340000);
  rex::ObserveRuntime("HUD","image_geometry",66,44,4,1,55);
  rex::ObserveRuntime("HUD","image_geometry",66,44,4,1,55);
  rex::ObserveRuntime("HUD","image_geometry",67,44,4,1,56);
  rex::ObserveRuntime("RESOURCE","request",100,0,0,0,0,"missing_hud");
  rex::ObserveRuntime("RESOURCE","final_failure",100,0,0,0,0,"missing_hud");
  spdlog::logger gpu("gpu.test",logger->sinks().begin(),logger->sinks().end());
  gpu.warn("Failed to create resource texture buffer");
  gpu.warn("Invalid fetch, shader pipeline failed");
  DumpRuntimeHistory("guest_fault_coverage_test", true);
  assert(capture->Count("body_descriptor_nonzero_characters=1 head_descriptor_nonzero_characters=0 body_all_zero_characters=0 head_all_zero_characters=1") >= 1);
  assert(capture->Count("fallback_successes=1 same_path_recoveries_approx=1 final_failures=1 native_open_failures=1") >= 1);
  assert(capture->Count("path=config\\example found_probes=1 probe_misses=0 native_open_successes=0 native_open_failures=1 same_path_recoveries_approx=0 pending_open_failure=true") >= 1);
  assert(capture->Count("image_name=hud_example") >= 1);
  assert(capture->Count("unique_image_instances=1 calls=2 interval_calls=2 geometry_draws=2") >= 1);
  assert(capture->Count("gpu_fetch_failures_logged=1 gpu_resource_failures_logged=1 gpu_shader_failures_logged=1 gpu_pipeline_failures_logged=1") >= 1);
  for (int i=0;i<1000;++i) rex::ObserveRuntime("RESOURCE","request",i,0,0,0,0,"synthetic");
  DumpRuntimeHistory("guest_fault_test", true);
  assert(capture->Count("reason=guest_fault_test events=512") == 1);
  StopRuntimeHealth();
  StopRuntimeHealth();
  assert(capture->Count("reason=shutdown ") == 1);
  assert(capture->Count("name=invalid_mixer_pcm active=true occurrences=40000 activations=2 resolutions=1") >= 1);
  size_t bytes = 0;
  for (const auto& line : capture->entries) bytes += line.size();
  assert(bytes < 2 * 1024 * 1024);
  assert(capture->Count("name=test_stub class=I-unknown count=4000") >= 1);
  assert(observer.load() == nullptr);
  puts("PASS: audio transition/gating, concurrent stubs, BODY/HEAD, HUD identity, VFS results, GPU log counters, history bound, shutdown");
}
