// Temporary DEBUG_SAULO runtime audit.
#include "runtime_health.h"

#include "audio_pcm_firewall.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include <rex/logging.h>
#include <spdlog/sinks/sink.h>

namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t kHistorySize = 512;
constexpr size_t kDumpedHistoryEvents = 48;
constexpr size_t kGroupLimit = 512;
constexpr size_t kPathLimit = 1024;

struct Event {
  uint64_t order = 0, ms = 0, object = 0;
  std::array<uint64_t, 4> data{};
  uint32_t caller = 0, thread = 0;
  char category[16]{}, name[48]{}, detail[192]{};
};
struct Group {
  uint64_t count = 0, object = 0, result = 0;
  uint32_t first_caller = 0;
  std::string behavior;
};
struct HudGroup {
  uint64_t calls = 0, draws = 0, vertices = 0, zero_output = 0, duplicate_in_pass = 0;
  uint64_t previous_calls = 0, style_vtable = 0;
  std::map<uint64_t, uint64_t> owners;
};
struct Character {
  std::array<std::array<std::array<uint32_t,4>,5>,2> words{};
  std::array<uint32_t,2> known{};
};
struct ResourcePath {
  uint64_t found = 0, misses = 0, opened = 0, open_failed = 0, recovered = 0, final_failed = 0;
  uint64_t first_unrecovered_ms = 0, last_status = 0;
  uint32_t first_caller = 0;
  bool pending = false, pending_open_failure = false;
};
struct ResourceScope {
  uint64_t object = 0, misses = 0;
  std::string path;
  std::array<std::string,8> attempts{};
  uint32_t attempt_count = 0;
};
using HudKey = std::tuple<uint64_t,uint64_t,uint32_t>;
struct Decoder {
  bool allocated = false, enabled = false, input = false, output = false, stalled = false;
  uint64_t last_work = 0, last_progress = 0, empty_decodes = 0, errors = 0;
  uint64_t packet_status = 0;
};
struct Incident {
  uint64_t occurrences = 0, activations = 0, resolutions = 0, last_seen = 0, last_report = 0;
  bool active = false;
};
struct Audio {
  uint64_t callbacks = 0, requested = 0, submitted = 0, completed = 0, discarded = 0, buffers = 0;
  uint64_t nonzero = 0, peak = 0, rms = 0, underruns = 0, invalid = 0;
  uint64_t mixer_nonzero = 0, mixer_peak = 0, mixer_invalid = 0;
  uint32_t mixer_clean_frames = 0, output_clean_frames = 0;
  std::array<uint64_t,6> mixer_channel_nan{}, mixer_channel_inf{}, mixer_channel_first_bits{};
  uint64_t clipped_samples = 0, shape_samples = 0, first_output_bits = 0;
  int64_t mean_micro = 0;
  uint32_t dc_frames = 0, dc_clean_frames = 0;
  uint64_t max_peak = 0, max_rms = 0, starvations = 0, silence_faults = 0, callback_stalls = 0, device_errors = 0;
  uint64_t frequency = 0, channels = 0, stream_channels = 0, format = 0, device_buffer_frames = 0;
  uint64_t last_callback = 0, last_submit = 0, last_active = 0, silent_since = 0;
  uint64_t queued = 0, backend_bytes = 0, gain = 1000;
  bool open = false, muted = false, paused = false, was_active = false, silence_reported = false;
};
struct State {
  std::mutex mutex;
  std::condition_variable wake;
  Clock::time_point start = Clock::now();
  std::array<Event, kHistorySize> history{};
  uint64_t order = 0, dropped = 0, next_history_sample = 0;
  uint64_t faults = 0, invalid_functions = 0, voices_created = 0, voices_destroyed = 0;
  uint64_t xma_created = 0, xma_released = 0, xma_packets = 0, xma_decodes = 0, xma_errors = 0;
  uint64_t vfs_requests = 0, vfs_found = 0, vfs_missing = 0;
  uint64_t gpu_rejects = 0, invalid_resolve = 0, xma_fills = 0, xma_pool_releases = 0;
  uint64_t candidate_count = 0, characters = 0, body_words = 0, head_words = 0;
  uint64_t logical_requests = 0, logical_success = 0, logical_failure = 0, fallback_success = 0;
  uint64_t vfs_recoveries = 0, vfs_open_failed = 0, gpu_fetch_failures = 0;
  uint64_t gpu_resource_failures = 0, gpu_shader_failures = 0, gpu_pipeline_failures = 0;
  Event last_fault;
  std::map<uint64_t, Audio> audio;
  std::map<std::string, Group> stubs, warnings, xma_callers;
  std::map<std::string, Incident> incidents;
  std::map<HudKey, HudGroup> hud;
  std::map<uint64_t,uint64_t> hud_styles;
  std::map<std::pair<uint64_t,uint64_t>,std::string> hud_names;
  std::map<uint64_t,Character> character_descriptors;
  std::map<uint64_t, Decoder> decoders;
  std::map<std::string, ResourcePath> paths;
  bool stopping = false, paused = false, gameplay = false;
  std::string pending_anomaly;
  uint64_t last_dump_ms = 0, dump_count = 0, suppressed_dumps = 0;
  uint64_t audio_monitor_after = 0;
};
State g;
std::jthread g_worker;
thread_local bool t_observing = false;
thread_local std::array<ResourceScope,8> t_resources;
thread_local uint32_t t_resource_depth = 0;
thread_local uint32_t t_resource_overflow = 0;
std::atomic<bool> g_sink_enabled{false};

uint64_t NowMs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g.start).count());
}
template <size_t N> void Copy(char (&out)[N], const char* in) {
  if (!in) return;
  const size_t n = std::min(std::strlen(in), N - 1);
  std::memcpy(out, in, n);
  out[n] = 0;
}
bool Anomaly(const char* reason) {
  auto& incident = g.incidents[reason];
  ++incident.occurrences;
  incident.last_seen = NowMs();
  if (incident.active) return false;
  incident.active = true;
  ++incident.activations;
  if (!incident.last_report || NowMs() - incident.last_report >= 10000) {
    if (g.pending_anomaly.empty()) g.pending_anomaly = reason;
    g.wake.notify_one();
  }
  return true;
}
void Resolve(const char* reason) {
  auto found = g.incidents.find(reason);
  if (found == g.incidents.end() || !found->second.active) return;
  found->second.active = false;
  ++found->second.resolutions;
  REXLOG_INFO("[DEBUG_SAULO][Anomaly] name={} transition=resolved", reason);
}

void History(const char* category, const char* name, uint64_t object,
             std::array<uint64_t, 4> data, const char* detail, uint32_t caller, uint32_t thread) {
  Event& e = g.history[g.order % kHistorySize];
  e = {};
  e.order = ++g.order;
  e.ms = NowMs();
  e.object = object;
  e.data = data;
  e.caller = caller;
  e.thread = thread;
  Copy(e.category, category);
  Copy(e.name, name);
  Copy(e.detail, detail);
}
std::string LogicalPath(std::string_view path) {
  std::string result(path.substr(0,240));
  for (char& ch : result) {
    if (ch == '/') ch = '\\';
    if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
  }
  if (result.starts_with("\\??\\")) result.erase(0,4);
  if (result.starts_with("game:\\")) result.erase(0,6);
  while (result.starts_with(".\\")) result.erase(0,2);
  return result;
}
ResourcePath* TrackPath(std::string_view path, uint32_t caller) {
  const std::string key = LogicalPath(path);
  if (!g.paths.contains(key) && g.paths.size() >= kPathLimit) { ++g.dropped; return nullptr; }
  auto& value = g.paths[key];
  if (!value.first_caller) value.first_caller = caller;
  return &value;
}
const char* StubClass(std::string_view name) {
  if (name.starts_with("NetDll_") || name.starts_with("XNet")) return "G-network";
  if (name.find("Floating") != name.npos || name.find("VectorUnit") != name.npos ||
      name.find("Context") != name.npos || name.find("Unwind") != name.npos) return "D-floating-context";
  if (name.find("XMA") != name.npos || name.find("Audio") != name.npos || name.find("Voice") != name.npos) return "E-audio";
  if (name.find("Stfs") != name.npos || name.find("File") != name.npos || name.find("Device") != name.npos) return "B-storage";
  if (name.find("Thread") != name.npos || name.find("Wait") != name.npos || name.find("Event") != name.npos || name.find("Semaphore") != name.npos || name.find("SpinLock") != name.npos) return "C-synchronization";
  if (name.find("Vd") != name.npos || name.find("Gpu") != name.npos) return "F-graphics";
  if (name.find("Net") != name.npos || name.find("Socket") != name.npos) return "G-network";
  if (name.find("Content") != name.npos || name.find("Profile") != name.npos || name.find("User") != name.npos) return "H-profile-content";
  if (name.find("Dbg") != name.npos || name.find("Debug") != name.npos || name.find("Perf") != name.npos) return "A-debug";
  return "I-unknown";
}

void ObserveImpl(const char* category, const char* event, uint64_t object, uint64_t a,
             uint64_t b, uint64_t c, uint64_t d, const char* detail, uint32_t caller, uint32_t thread) {
  if (t_observing) return;
  t_observing = true;
  bool fatal = false;
  std::string first_stub;
  {
    std::lock_guard lock(g.mutex);
    const uint64_t now = NowMs();
    const std::string_view cat(category), name(event);
    bool record = true;
    if (cat == "AUDIO") {
      if (name == "source_created") ++g.voices_created;
      else if (name == "source_destroyed") ++g.voices_destroyed;
      else if (name == "paused") g.paused = true;
      else if (name == "resumed") { g.paused = false; g.audio_monitor_after = now + 500; }
      else if (name == "pump_begin" || name == "pump_end") record = now >= g.next_history_sample;
      else if (g.audio.contains(object) || g.audio.size() < 32) {
        Audio& out = g.audio[object];
        if (name == "device_open") { out.open = true; out.frequency = a; out.channels = b; out.stream_channels = c; out.format = d; }
        else if (name == "device_buffer") out.device_buffer_frames = a;
        else if (name == "device_closed") out.open = false;
        else if (name == "buffer_submitted") { ++out.buffers; out.last_submit = now; out.queued = b; record = now >= g.next_history_sample; }
        else if (name == "buffer_completed") { ++out.completed; out.queued = a; record = false; }
        else if (name == "buffer_discarded") { ++out.discarded; out.queued = a; Anomaly("audio_device_error"); }
        else if (name == "callback") {
          ++out.callbacks; out.requested += a; out.last_callback = now; out.backend_bytes = b;
          const bool muted = (d >> 32) != 0, paused = c != 0;
          record = out.muted != muted || out.paused != paused || out.gain != uint32_t(d);
          out.muted = muted; out.paused = paused; out.gain = uint32_t(d);
        } else if (name == "mixer_channel") {
          record = false;
          if (a < 6) {
            out.mixer_channel_nan[a] += c; out.mixer_channel_inf[a] += b - c;
            if (b && !out.mixer_channel_first_bits[a]) out.mixer_channel_first_bits[a] = d;
          }
        } else if (name == "pcm_shape") {
          out.clipped_samples += a; out.shape_samples += b;
          out.mean_micro = int64_t(c); out.first_output_bits = d;
          record = false;
          if (b && a * 10 >= b * 9 && (out.mean_micro >= 990000 || out.mean_micro <= -990000)) {
            out.dc_clean_frames = 0;
            if (++out.dc_frames >= 32) record = Anomaly("audio_output_dc_saturated");
          } else {
            out.dc_frames = 0;
            if (++out.dc_clean_frames == 32) Resolve("audio_output_dc_saturated");
          }
        } else if (name == "mixer_pcm") {
          out.mixer_nonzero += a; out.mixer_peak = b; out.mixer_invalid += c;
          if (c) { out.mixer_clean_frames = 0; record = Anomaly("invalid_mixer_pcm"); }
          else { record = false; if (++out.mixer_clean_frames == 32) Resolve("invalid_mixer_pcm"); }
        } else if (name == "pcm") {
          const uint64_t previous_peak = out.peak;
          out.submitted += a; out.nonzero += b; out.peak = c; out.rms = d & ~(uint64_t(1) << 63);
          out.max_peak = std::max(out.max_peak,c); out.max_rms = std::max(out.max_rms,out.rms);
          out.invalid += d >> 63;
          if (d >> 63) { out.output_clean_frames = 0; Anomaly("invalid_pcm"); }
          else if (++out.output_clean_frames == 32) Resolve("invalid_pcm");
          if (b) { record = !out.was_active || out.silence_reported; out.was_active = true;
            out.last_active = now; out.silent_since = 0; out.silence_reported = false;
          } else { record = false; if (!out.silent_since) out.silent_since = now; }
          record |= c > 2'000'000 && previous_peak <= 2'000'000;
        } else if (name == "underrun") {
          ++out.underruns; record = out.underruns == 1;
          if (now >= g.audio_monitor_after && out.was_active && !g.paused && !out.paused && !out.muted && out.gain &&
              now - out.last_active > 500 && !out.silence_reported) {
            ++out.starvations; out.silence_reported = true; Anomaly("audio_starvation"); record = true;
          }
        } else if (name == "device_error") { ++out.device_errors; Anomaly("audio_device_error"); }
      }
    } else if (cat == "XMA") {
      if (object && !name.starts_with("pool_") && (g.decoders.contains(object) || g.decoders.size() < 512)) {
        auto& decoder = g.decoders[object];
        if (name == "created") { decoder = {}; decoder.allocated = true; decoder.last_progress = now; }
        else if (name == "released") decoder.allocated = decoder.enabled = false;
        else if (name == "enabled") { decoder.enabled = true; decoder.last_progress = now; decoder.stalled = false; }
        else if (name == "disabled") decoder.enabled = false;
        else if (name == "work") {
          decoder.last_work = now; decoder.input = a || b; decoder.output = c != 0;
          if (d && d != decoder.packet_status) {
            ++g.xma_errors;
            History("XMA", "invalid_packet_state", object, {d,a,b,c}, nullptr, caller, thread);
            Anomaly("xma_invalid_packet_state");
          }
          decoder.packet_status = d;
        }
        else if (name == "output" && a != b) { decoder.last_progress = now; decoder.stalled = false; }
        else if (name == "decode" && !b) ++decoder.empty_decodes;
        else if (name == "decode_error") ++decoder.errors;
      }
      if (name == "created") ++g.xma_created;
      else if (name == "released") ++g.xma_released;
      else if (name == "packet") { ++g.xma_packets; record = now >= g.next_history_sample; }
      else if (name == "decode") { ++g.xma_decodes; record = now >= g.next_history_sample; }
      else if (name == "decode_error") { ++g.xma_errors; Anomaly("xma_decode_error"); }
      else if (name == "pool_exhausted") Anomaly("xma_pool_exhausted");
      else if (name == "pool_fill") { ++g.xma_fills; if (c == 0 && b > 0) Anomaly("title_xma_pool_saturated"); }
      else if (name == "pool_release") ++g.xma_pool_releases;
      else record = now >= g.next_history_sample;
      if (name == "decode_error" && g.xma_callers.size() < kGroupLimit) {
        const auto key = fmt::format("{:08X}:{:X}", caller, a);
        ++g.xma_callers[key].count;
      }
    } else if (cat == "VFS") {
      const std::string_view path = detail ? detail : "?";
      const bool open = name == "open_result";
      const bool success = open ? !(uint32_t(a) & 0x80000000u) : name == "probe_found";
      if (!open) { ++g.vfs_requests; success ? ++g.vfs_found : ++g.vfs_missing; }
      else if (!success) ++g.vfs_open_failed;
      if (auto* p = TrackPath(path,caller)) {
        record = open ? (success ? p->opened++ : p->open_failed++) == 0
                      : (success ? p->found++ : p->misses++) == 0;
        if (success && p->pending && (open || !p->pending_open_failure)) {
          ++p->recovered; ++g.vfs_recoveries; p->pending = p->pending_open_failure = false;
          History("VFS", "same_path_recovered", object, {a,0,0,0}, detail, caller, thread);
        } else if (!success) {
          if (!p->pending) p->first_unrecovered_ms = now;
          p->pending = true; p->pending_open_failure |= open;
          p->last_status = open ? a : p->last_status;
        }
      } else record = false;
      if (t_resource_depth && !t_resource_overflow) {
        auto& scope = t_resources[t_resource_depth-1];
        scope.misses += !success;
        if (scope.attempt_count < scope.attempts.size())
          scope.attempts[scope.attempt_count++] = fmt::format("{}:{}:{:08X}",path,event,uint32_t(a));
      }
    } else if (cat == "RESOURCE") {
      if (name == "request") {
        ++g.logical_requests;
        if (t_resource_depth < t_resources.size() && !t_resource_overflow) {
          auto& scope = t_resources[t_resource_depth++]; scope = {}; scope.object = object;
          scope.path = detail ? std::string_view(detail).substr(0,240) : "?";
        } else { ++t_resource_overflow; ++g.dropped; }
      } else if (name == "success" || name == "final_failure" || name == "result_unreadable") {
        bool recovered = a != 0;
        if (t_resource_overflow) --t_resource_overflow;
        else if (t_resource_depth && t_resources[t_resource_depth-1].object == object) {
          auto& scope = t_resources[--t_resource_depth];
          recovered |= scope.misses != 0;
          for (uint32_t i=0;i<scope.attempt_count;++i)
            History("RESOURCE","attempt",object,{i,0,0,0},scope.attempts[i].c_str(),caller,thread);
        }
        if (name == "success") {
          ++g.logical_success; g.fallback_success += recovered;
          if (auto* path = TrackPath(detail ? detail : "?",caller)) path->pending = path->pending_open_failure = false;
          const auto key = std::make_pair(b,c);
          if (b && (g.hud_names.contains(key) || g.hud_names.size() < 512))
            g.hud_names[key] = detail ? std::string_view(detail).substr(0,240) : "?";
        } else if (name == "final_failure") {
          ++g.logical_failure;
          if (auto* path = TrackPath(detail ? detail : "?",caller)) ++path->final_failed;
          Anomaly("resource_final_failure");
        }
      }
    } else if (cat == "CHARACTER") {
      if (name == "constructed") {
        ++g.characters;
        if (object && (g.character_descriptors.contains(object) || g.character_descriptors.size() < 1024))
          g.character_descriptors.try_emplace(object);
        else ++g.dropped;
      }
      else if (name == "body_descriptor") g.body_words += a;
      else if (name == "head_descriptor") g.head_words += a;
      else if (name == "descriptor_words" && object && (g.character_descriptors.contains(object) || g.character_descriptors.size() < 1024)) {
        const uint32_t side = (a >> 8) & 1, level = a & 0xFF;
        if (level < 5 && d) {
          auto& character = g.character_descriptors[object];
          character.words[side][level] = {uint32_t(b >> 32),uint32_t(b),uint32_t(c >> 32),uint32_t(c)};
          character.known[side] |= 1u << level;
        }
      }
    } else if (cat == "HUD") {
      if (name == "candidates") { record = g.candidate_count != a; g.candidate_count = a; }
      else if (name == "image_identity") {
        record = false;
        if (g.hud_styles.contains(object) || g.hud_styles.size() < 1024) g.hud_styles[object] = c;
      }
      else if (name == "image_geometry") {
        const HudKey key(a,d,caller);
        if (g.hud.contains(key) || g.hud.size() < 128) {
          auto& group = g.hud[key];
          record = group.calls++ == 0;
          group.draws += b != 0;
          if (auto style = g.hud_styles.find(object); style != g.hud_styles.end()) group.style_vtable = style->second;
          group.vertices += b; group.zero_output += b == 0;
          if (group.owners.contains(object) || group.owners.size() < 256) {
            auto& last_pass = group.owners[object];
            group.duplicate_in_pass += c != 0 && last_pass == c;
            last_pass = c;
          } else ++g.dropped;
          if (group.owners.size() == 256) Anomaly("hud_owner_registry_saturated");
        } else { ++g.dropped; record = false; }
      }
    } else if (cat == "GPU") {
      if (name == "invalid_resolve") { ++g.invalid_resolve; record = g.invalid_resolve == 1; }
      else if (name == "draw_rejected") { ++g.gpu_rejects; Anomaly("gpu_draw_rejected"); }
    } else if (cat == "KERNEL" && name.starts_with("stub")) {
      const std::string key(detail ? std::string_view(detail).substr(0, 160) : event);
      if (g.stubs.contains(key) || g.stubs.size() < kGroupLimit) {
        auto& s = g.stubs[key];
        record = s.count++ == 0;
        if (record) { s.first_caller = caller; s.result = a; s.object = object; s.behavior = event; first_stub = key; }
      } else { ++g.dropped; record = false; }
    } else if (cat == "FAULT") {
      ++g.faults; g.invalid_functions += name.starts_with("invalid_");
      g.last_fault = {}; g.last_fault.object = object; g.last_fault.caller = caller;
      g.last_fault.thread = thread; Copy(g.last_fault.name,event);
      Anomaly(event); fatal = true;
    } else if (cat == "THREAD") Anomaly(event);
    if (record) {
      History(category, event, object, {a,b,c,d}, detail, caller, thread);
      if (cat == "AUDIO" || cat == "XMA") g.next_history_sample = now + 100;
    }
  }
  t_observing = false;
  if (!first_stub.empty())
    REXLOG_INFO("[DEBUG_SAULO][StubAudit] first={} class={} caller={:08X} behavior={} value={:X}",
                first_stub, StubClass(first_stub), caller, event, a);
  if (fatal) DumpRuntimeHistory(event, true);
}

void Observe(const char* category, const char* event, uint64_t object, uint64_t a,
             uint64_t b, uint64_t c, uint64_t d, const char* detail, uint32_t caller, uint32_t thread) noexcept {
  try { ObserveImpl(category, event, object, a, b, c, d, detail, caller, thread); }
  catch (...) { t_observing = false; }
}

class AuditSink final : public spdlog::sinks::sink {
 public:
  void log(const spdlog::details::log_msg& msg) override {
    if (!g_sink_enabled.load(std::memory_order_relaxed) || t_observing || msg.level < spdlog::level::warn) return;
    std::string_view payload(msg.payload.data(), msg.payload.size());
    if (payload.find("[DEBUG_SAULO]") != payload.npos) return;
    const std::string_view logger(msg.logger_name.data(), msg.logger_name.size());
    const char* category = logger.starts_with("gpu") ? "GPU" : logger.starts_with("apu") ? "AUDIO" : "KERNEL";
    if (payload.find("stub") != payload.npos || payload.find("STUB") != payload.npos) {
      if (payload.starts_with("[STUB] ")) payload.remove_prefix(7);
      const size_t end = payload.find_first_of(" (:");
      const std::string name(payload.substr(0, end));
      rex::ObserveRuntime("KERNEL", "stub_logged", 0, UINT64_MAX, 0, 0, 0, name.c_str());
      return;
    }
    std::string normalized(category);
    normalized += ':';
    normalized += payload.substr(0, 160);
    for (char& ch : normalized) if (ch >= '0' && ch <= '9') ch = '#';
    bool first = false;
    {
      std::lock_guard lock(g.mutex);
      if (logger.starts_with("gpu")) {
        std::string text(payload.substr(0, 512));
        for (char& ch : text) if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
        const bool failed = text.find("fail") != text.npos || text.find("invalid") != text.npos ||
                            text.find("unable") != text.npos || text.find("could not") != text.npos;
        if (failed) {
          g.gpu_fetch_failures += text.find("fetch") != text.npos;
          g.gpu_resource_failures += text.find("resource") != text.npos || text.find("texture") != text.npos ||
                                    text.find("buffer") != text.npos || text.find("descriptor") != text.npos;
          g.gpu_shader_failures += text.find("shader") != text.npos;
          g.gpu_pipeline_failures += text.find("pipeline") != text.npos;
        }
      }
      if (g.warnings.contains(normalized) || g.warnings.size() < kGroupLimit)
        first = g.warnings[normalized].count++ == 0;
      else ++g.dropped;
    }
    if (first) Observe(category, "warning", 0, uint64_t(msg.level), 0, 0, 0, normalized.c_str(), 0, 0);
  }
  void flush() override {}
  void set_pattern(const std::string&) override {}
  void set_formatter(std::unique_ptr<spdlog::formatter>) override {}
};
std::shared_ptr<AuditSink> g_sink;

void Snapshot(const char* reason) {
  std::vector<std::pair<uint64_t, Audio>> audio;
  std::vector<std::pair<std::string, Incident>> incidents;
  std::vector<std::pair<std::string, Group>> stubs, warnings, xma_callers;
  std::vector<std::pair<std::string, ResourcePath>> paths;
  std::vector<std::pair<HudKey, HudGroup>> hud;
  std::vector<std::pair<uint64_t, Character>> characters;
  std::map<std::pair<uint64_t,uint64_t>,std::string> hud_names;
  uint64_t enabled_decoders = 0, stalled_decoders = 0, empty_decodes = 0;
  uint64_t nonzero[2]{}, zero[2]{}, unknown[2]{}, pending_open_groups = 0;
  Event fault;
  std::string counters;
  const auto pcm = crackdown::AudioPcmFirewallCounts();
  {
    std::lock_guard lock(g.mutex);
    for (const auto& [id, character] : g.character_descriptors) {
      (void)id;
      for (size_t side = 0; side < 2; ++side) {
        bool any = false;
        for (const auto& words : character.words[side])
          for (uint32_t word : words) any |= word != 0;
        if (any) ++nonzero[side];
        else if (character.known[side] == 31) ++zero[side];
        else ++unknown[side];
      }
    }
    for (const auto& [path, value] : g.paths) {
      (void)path;
      pending_open_groups += value.pending_open_failure && NowMs() - value.first_unrecovered_ms >= 10000;
    }
    counters = fmt::format("elapsed_ms={} faults={} invalid_functions={} sources_created={} sources_destroyed={} "
        "xma_contexts_created={} xma_contexts_released={} xma_packets={} xma_decodes={} xma_errors={} "
        "title_xma_fills={} title_xma_releases={} gpu_draw_rejects={} invalid_resolve={} "
        "gpu_fetch_failures_logged={} gpu_resource_failures_logged={} gpu_shader_failures_logged={} gpu_pipeline_failures_logged={} "
        "character_profiles_constructed={} npc_population=unproven body_descriptor_nonzero_characters={} head_descriptor_nonzero_characters={} "
        "body_all_zero_characters={} head_all_zero_characters={} body_unknown_characters={} head_unknown_characters={} "
        "body_descriptor_nonzero_observations={} head_descriptor_nonzero_observations={} "
        "candidates={} vfs_probes={} found={} probe_misses={} logical_requests={} logical_success={} "
        "fallback_successes={} same_path_recoveries_approx={} final_failures={} native_open_failures={} "
        "unrecovered_open_groups_after_10s={} unique_stubs={} dropped={} history_dumps={} suppressed_dumps={} "
        "sanitized_nan={} sanitized_inf={} clamped_high={} clamped_low={} sanitized_frames={}",
        NowMs(), g.faults, g.invalid_functions, g.voices_created, g.voices_destroyed,
        g.xma_created, g.xma_released, g.xma_packets, g.xma_decodes, g.xma_errors,
        g.xma_fills, g.xma_pool_releases, g.gpu_rejects, g.invalid_resolve,
        g.gpu_fetch_failures, g.gpu_resource_failures, g.gpu_shader_failures, g.gpu_pipeline_failures,
        g.characters, nonzero[0], nonzero[1], zero[0], zero[1], unknown[0], unknown[1],
        g.body_words, g.head_words, g.candidate_count, g.vfs_requests, g.vfs_found, g.vfs_missing,
        g.logical_requests, g.logical_success, g.fallback_success, g.vfs_recoveries, g.logical_failure,
        g.vfs_open_failed, pending_open_groups, g.stubs.size(), g.dropped, g.dump_count, g.suppressed_dumps,
        pcm.sanitized_nan, pcm.sanitized_inf, pcm.clamped_high, pcm.clamped_low, pcm.sanitized_frames);
    fault = g.last_fault;
    audio.assign(g.audio.begin(), g.audio.end());
    incidents.assign(g.incidents.begin(),g.incidents.end());
    stubs.assign(g.stubs.begin(), g.stubs.end());
    warnings.assign(g.warnings.begin(), g.warnings.end());
    paths.assign(g.paths.begin(), g.paths.end());
    hud.assign(g.hud.begin(), g.hud.end());
    for (auto& [key, group] : g.hud) { (void)key; group.previous_calls = group.calls; }
    hud_names = g.hud_names;
    characters.assign(g.character_descriptors.begin(),g.character_descriptors.end());
    xma_callers.assign(g.xma_callers.begin(), g.xma_callers.end());
    for (auto& [id, decoder] : g.decoders) {
      (void)id;
      enabled_decoders += decoder.allocated && decoder.enabled;
      stalled_decoders += decoder.stalled;
      empty_decodes += decoder.empty_decodes;
    }
  }
  REXLOG_INFO("[DEBUG_SAULO][RuntimeHealth] reason={} {}", reason, counters);
  for (const auto& [name, incident] : incidents)
    REXLOG_INFO("[DEBUG_SAULO][Anomaly] name={} active={} occurrences={} activations={} resolutions={}",
                name,incident.active,incident.occurrences,incident.activations,incident.resolutions);
  REXLOG_INFO("[DEBUG_SAULO][FaultHealth] last_fault={} object={:X} caller={:08X} thread={}",
              fault.name, fault.object, fault.caller, fault.thread);
  REXLOG_INFO("[DEBUG_SAULO][XMA] observed_enabled_contexts={} stalled={} zero_sample_decode_results={}",
               enabled_decoders, stalled_decoders, empty_decodes);
  for (auto& [key, group] : xma_callers)
    REXLOG_INFO("[DEBUG_SAULO][XMA] error_caller_and_status={} count={}", key, group.count);
  for (auto& [id, a] : audio) {
    const uint64_t now = NowMs();
    REXLOG_INFO("[DEBUG_SAULO][AudioOutput] device={:X} open={} paused={} mute={} gain_milli={} "
        "sample_rate={} device_channels={} stream_channels={} format={:X} device_buffer_frames={} "
        "callbacks={} requested_frames={} submitted_frames={} nonzero_samples={} peak_micro={} rms_micro={} "
        "max_peak_micro={} max_rms_micro={} mixer_nonzero_samples={} mixer_peak_micro={} mixer_invalid_pcm={} "
        "submitted_buffers={} completed_buffers={} discarded_buffers={} queued={} backend_bytes={} underruns={} "
        "starvations={} silence_faults={} callback_stalls={} device_errors={} invalid_pcm={} "
        "last_callback_age_ms={} last_submit_age_ms={} silent_ms={}",
        id, a.open, a.paused, a.muted, a.gain, a.frequency, a.channels, a.stream_channels, a.format,
        a.device_buffer_frames, a.callbacks, a.requested, a.submitted,
        a.nonzero, a.peak, a.rms, a.max_peak, a.max_rms, a.mixer_nonzero, a.mixer_peak, a.mixer_invalid,
        a.buffers, a.completed, a.discarded, a.queued, a.backend_bytes, a.underruns,
        a.starvations, a.silence_faults, a.callback_stalls, a.device_errors, a.invalid,
        now-a.last_callback, now-a.last_submit, a.was_active ? now-a.last_active : 0);
    REXLOG_INFO("[DEBUG_SAULO][AudioShape] clipped_samples={} samples={} mean_micro={} first_sample_bits={:08X}",
                a.clipped_samples,a.shape_samples,a.mean_micro,a.first_output_bits);
    for (size_t ch=0;ch<6;++ch)
      REXLOG_INFO("[DEBUG_SAULO][MixerChannel] channel={} nan_samples={} inf_samples={} first_invalid_bits={:08X}",
                  ch,a.mixer_channel_nan[ch],a.mixer_channel_inf[ch],a.mixer_channel_first_bits[ch]);
  }
  for (size_t i=0;i<std::min<size_t>(characters.size(),4);++i) {
    const auto& [id, character] = characters[i];
    for (size_t side=0;side<2;++side) {
      for (size_t level=0;level<5;++level) {
        const auto& w = character.words[side][level];
        REXLOG_INFO("[DEBUG_SAULO][CharacterHealth] profile_address={:08X} side={} level={} readable={} "
                    "words={:08X},{:08X},{:08X},{:08X} semantics=resource_record_selectors",
                    id, side ? "HEAD" : "BODY", level+1, bool(character.known[side] & (1u << level)),
                    w[0],w[1],w[2],w[3]);
      }
    }
  }
  std::sort(stubs.begin(), stubs.end(), [](const auto& a, const auto& b) { return a.second.count > b.second.count; });
  for (const auto& [name, s] : stubs)
    REXLOG_INFO("[DEBUG_SAULO][StubAudit] name={} class={} count={} first_caller={:08X} first_r3={:X} behavior={} value={:X}",
        name, StubClass(name), s.count, s.first_caller, s.object, s.behavior, s.result);
  std::sort(warnings.begin(), warnings.end(), [](const auto& a, const auto& b) { return a.second.count > b.second.count; });
  for (size_t i = 0; i < std::min<size_t>(warnings.size(), 16); ++i)
    REXLOG_INFO("[DEBUG_SAULO][{}] warning_group={} count={}",
        warnings[i].first.starts_with("GPU:") ? "GPUHealth" : "RuntimeHealth",
        warnings[i].first, warnings[i].second.count);
  std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.second.final_failed > b.second.final_failed; });
  for (size_t i = 0; i < std::min<size_t>(paths.size(),16) && paths[i].second.final_failed; ++i)
    REXLOG_INFO("[DEBUG_SAULO][ResourceResolve] final_failure_path={} count={} first_caller={:08X}",
                paths[i].first,paths[i].second.final_failed,paths[i].second.first_caller);
  std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.second.misses + a.second.open_failed > b.second.misses + b.second.open_failed; });
  for (size_t i = 0; i < std::min<size_t>(paths.size(), 16); ++i) {
    const auto& [path,p] = paths[i];
    REXLOG_INFO("[DEBUG_SAULO][ResourceResolve] path={} found_probes={} probe_misses={} native_open_successes={} "
                "native_open_failures={} same_path_recoveries_approx={} pending_open_failure={} last_status={:08X} "
                "first_caller={:08X} final_result={}", path,p.found,p.misses,p.opened,p.open_failed,p.recovered,
                p.pending_open_failure,p.last_status,p.first_caller,p.final_failed ? "confirmed_failure" : "unproven");
  }
  std::sort(hud.begin(),hud.end(),[](const auto& a,const auto& b) { return a.second.calls > b.second.calls; });
  for (size_t i=0;i<std::min<size_t>(hud.size(),24);++i) {
    const auto& [key,group] = hud[i];
    const auto [texture,identity,caller] = key;
    const auto name = hud_names.find({texture,identity});
    REXLOG_INFO("[DEBUG_SAULO][HUDHealth] boundary=823D0FD0 texture={:08X} image_descriptor_id={:016X} "
        "image_name={} atlas_candidate=hudtextures.tpk caller={:08X} style_vtable={:08X} "
        "unique_image_instances={} calls={} interval_calls={} geometry_draws={} vertices={} zero_output={} "
        "duplicate_image_in_known_pass={} target_owner=unproven", texture,identity,
        name == hud_names.end() ? "unknown" : name->second,caller,group.style_vtable,
        group.owners.size(),group.calls,group.calls-group.previous_calls,group.draws,
        group.vertices,group.zero_output,group.duplicate_in_pass);
  }
}

void Worker(std::stop_token stop) {
  uint32_t snapshots = 0;
  uint64_t next_snapshot = 0;
  std::unique_lock lock(g.mutex);
  while (!g.stopping && !stop.stop_requested()) {
    g.wake.wait_for(lock, std::chrono::milliseconds(100), [] { return g.stopping || !g.pending_anomaly.empty(); });
    const uint64_t now = NowMs();
    for (auto& [id, out] : g.audio) {
      (void)id;
      if (now < g.audio_monitor_after || !out.open || !out.was_active || g.paused || out.paused || out.muted || !out.gain || out.silence_reported) continue;
      if (now - out.last_active > 500 && (now - out.last_submit < 500 || now - out.last_callback > 500)) {
        out.silence_reported = true;
        if (now - out.last_callback > 500) ++out.callback_stalls;
        else ++out.silence_faults;
        Anomaly(now - out.last_callback > 500 ? "audio_callback_stalled" : "silence_detected");
      }
    }
    for (auto& [id, decoder] : g.decoders) {
      if (decoder.allocated && decoder.input && decoder.output && now - decoder.last_work < 500 &&
          now - decoder.last_progress > 1000 && !decoder.stalled) {
        decoder.stalled = true;
        History("XMA", "no_progress", id, {decoder.last_work,decoder.last_progress,0,0}, nullptr, 0, 0);
        Anomaly("xma_no_progress");
      }
    }
    for (auto& [name,incident] : g.incidents) {
      if (incident.active && name != "invalid_mixer_pcm" && name != "invalid_pcm" &&
          now - incident.last_seen >= 1000) Resolve(name.c_str());
    }
    std::string reason = std::move(g.pending_anomaly);
    if (!reason.empty()) g.incidents[reason].last_report = now ? now : 1;
    g.pending_anomaly.clear();
    const bool periodic = g.gameplay && snapshots < 12 && now >= next_snapshot;
    if (periodic) { next_snapshot = now + 5000; ++snapshots; }
    lock.unlock();
    if (!reason.empty()) {
      if (reason.starts_with("audio_") || reason == "silence_detected" || reason == "invalid_pcm")
        REXLOG_ERROR("[DEBUG_SAULO][AudioFault] {}", reason);
      else REXLOG_ERROR("[DEBUG_SAULO][RuntimeHealth] anomaly={}", reason);
      DumpRuntimeHistory(reason.c_str());
    }
    if (periodic) Snapshot("periodic");
    lock.lock();
  }
}
}

void DumpRuntimeHistory(const char* reason, bool fault) {
  std::vector<Event> events;
  {
    std::unique_lock lock(g.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    const uint64_t now = NowMs();
    if (!fault && (g.dump_count >= 24 || (g.dump_count && now - g.last_dump_ms < 10000))) { ++g.suppressed_dumps; return; }
    ++g.dump_count;
    g.last_dump_ms = now;
    const uint64_t first = g.order > kDumpedHistoryEvents ? g.order - kDumpedHistoryEvents : 0;
    for (uint64_t i = first; i < g.order; ++i) events.push_back(g.history[i % kHistorySize]);
  }
  REXLOG_ERROR("[DEBUG_SAULO][RuntimeHistory] reason={} events={}", reason, events.size());
  for (const Event& e : events)
    REXLOG_INFO("[DEBUG_SAULO][RuntimeHistory] #{} t={} thread={} category={} event={} object={:X} "
        "caller={:08X} payload={:X},{:X},{:X},{:X} detail={}", e.order, e.ms, e.thread,
        e.category, e.name, e.object, e.caller, e.data[0], e.data[1], e.data[2], e.data[3], e.detail);
  Snapshot(reason);
  rex::FlushLogging();
}

void StartRuntimeHealth() {
  g_sink = std::make_shared<AuditSink>();
  rex::AddSink(g_sink);
  g_sink_enabled = true;
  rex::SetRuntimeObserver(Observe);
  g_worker = std::jthread([](std::stop_token stop) {
    try { Worker(stop); }
    catch (...) { g_sink_enabled = false; rex::SetRuntimeObserver(nullptr); }
  });
  REXLOG_INFO("[DEBUG_SAULO][RuntimeHealth] installed history=512 groups=512 paths=1024 periodic_limit=12 interval_ms=5000");
}
void RuntimeHealthGameplayStarted() {
  std::lock_guard lock(g.mutex);
  if (!g.gameplay) g.gameplay = true;
}
void StopRuntimeHealth() {
  static std::once_flag stopped;
  std::call_once(stopped, [] {
    {
      std::lock_guard lock(g.mutex);
      g.stopping = true;
      g.wake.notify_one();
    }
    if (g_worker.joinable()) g_worker.join();
    Snapshot("shutdown");
    g_sink_enabled = false;
    rex::SetRuntimeObserver(nullptr);
    g_sink.reset();
    rex::FlushLogging();
  });
}
