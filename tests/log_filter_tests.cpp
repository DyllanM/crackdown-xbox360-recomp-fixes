// Detailed logging: the setting value and fallback, the Minimal filter's classification and its
// effect on the rotating log file, and the session-log lifecycle that keeps a crash log but discards
// a normal-session log when Detailed logging is off. Run: crackdown_log_filter_tests

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "active_log.h"
#include "crash_report.h"
#include "log_filter.h"
#include "session_log.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(CRACKDOWN_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

std::string FunctionBody(const std::string& source, const char* declaration) {
  const size_t start = source.find(declaration);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find("\n  }\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string ReadText(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteText(const std::filesystem::path& file, const std::string& text) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << text;
}

void SetDetailedLogging(bool enabled) {
  rex::cvar::SetFlagByName("crackdown_detailed_logging", enabled ? "true" : "false");
  crackdown::ApplyDetailedLogging();
}

void TestClassification() {
  std::printf("Minimal mode keeps failures and first-line diagnostics\n");
  using spdlog::level::debug;
  using spdlog::level::err;
  using spdlog::level::info;
  using spdlog::level::trace;
  using spdlog::level::warn;

  CHECK(!crackdown::ShouldLogInMinimalMode(info, "core", "Launcher shown"));
  CHECK(!crackdown::ShouldLogInMinimalMode(info, "core", "Scheduling: high priority"));
  CHECK(!crackdown::ShouldLogInMinimalMode(info, "core", "Initializing shader storage for title 4D5307DC..."));
  CHECK(!crackdown::ShouldLogInMinimalMode(info, "gpu", "GPU wait for idle = 00000000"));
  CHECK(!crackdown::ShouldLogInMinimalMode(info, "krnl", "Function table initialized for module: code=..."));
  CHECK(!crackdown::ShouldLogInMinimalMode(debug, "core", "RexApp starting"));
  CHECK(!crackdown::ShouldLogInMinimalMode(trace, "core", "per-instruction detail"));

  CHECK(crackdown::ShouldLogInMinimalMode(warn, "core", "Window opened on another display"));
  CHECK(crackdown::ShouldLogInMinimalMode(err, "gpu", "Failed to create a Direct3D 12 device"));
  CHECK(crackdown::ShouldLogInMinimalMode(spdlog::level::critical, "core", "fatal"));

  CHECK(crackdown::ShouldLogInMinimalMode(
      info, "core", "Crackdown port 0.4.0 (x86-64, Direct3D 12, ReXGlue SDK 0.10.0)"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "User data: C:\\game\\user_data"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "Player settings: C:\\game\\user_data\\settings.toml"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "Graphics card: Test GPU (adapter 0)"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "Internal render resolution: 75% 960x540 -> 1280x720"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "Aspect ratio: 16:9 (1280x720)"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "Frame rate: 60 FPS, 1 vblank(s) per frame"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "gpu", "DXGI adapter: Test GPU (vendor 0x1002, device 0x1234)"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "Vulkan device: Test GPU (type DiscreteGpu)"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "SDL video driver: windows"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "Windows crash reporting enabled for current log."));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "Execution complete"));
  CHECK(crackdown::ShouldLogInMinimalMode(info, "core", "Window closing, shutting down..."));
}

void TestSettingValue(const std::filesystem::path& dir) {
  std::printf("setting default, persistence and malformed fallback\n");
  const std::filesystem::path file = dir / "settings.toml";

  rex::cvar::ResetToDefault("crackdown_detailed_logging");
  CHECK(rex::cvar::GetFlagByName("crackdown_detailed_logging") == "true");
  CHECK(crackdown::DetailedLoggingEnabled());

  WriteText(file, "crackdown_detailed_logging = false\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("crackdown_detailed_logging") == "false");
  CHECK(!crackdown::DetailedLoggingEnabled());

  WriteText(file, "crackdown_detailed_logging = true\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("crackdown_detailed_logging") == "true");
  CHECK(crackdown::DetailedLoggingEnabled());

  rex::cvar::ResetToDefault("crackdown_detailed_logging");
  WriteText(file, "# no detailed logging here\nwindow_width = 1280\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("crackdown_detailed_logging") == "true");
  CHECK(crackdown::DetailedLoggingEnabled());

  rex::cvar::ResetToDefault("crackdown_detailed_logging");
  WriteText(file, "crackdown_detailed_logging = \"garbage\"\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("crackdown_detailed_logging") == "true");
  CHECK(crackdown::DetailedLoggingEnabled());

  rex::cvar::ResetToDefault("crackdown_detailed_logging");
  WriteText(file, "crackdown_detailed_logging = 1\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("crackdown_detailed_logging") == "true");
  CHECK(crackdown::DetailedLoggingEnabled());
}

void TestCrashReportIndependent() {
  std::printf("CRASH REPORT formatting does not depend on the setting\n");
  crackdown::CrashReport report;
  report.time = {2026, 10, 19, 12, 0, 0, 0};
  report.code = 0xC0000005u;
  report.address = 0x00007FF6A1B2C3D4ull;
  report.thread_id = 7;
  report.process_id = 9;
  report.architecture = "x86-64";
  report.game_version = "0.4.0";

  char detailed[8192];
  const size_t detailed_length = crackdown::FormatCrashReport(report, detailed, sizeof(detailed));

  SetDetailedLogging(false);
  char minimal[8192];
  const size_t minimal_length = crackdown::FormatCrashReport(report, minimal, sizeof(minimal));
  CHECK(detailed_length == minimal_length);
  CHECK(std::string(detailed, detailed_length) == std::string(minimal, minimal_length));
  CHECK(Has(std::string(detailed, detailed_length), "CRASH REPORT\n"));
  CHECK(Has(std::string(detailed, detailed_length), "Exception code: 0xC0000005 (ACCESS_VIOLATION)"));
  CHECK(Has(std::string(detailed, detailed_length), "Thread ID: 7"));
}

spdlog::sink_ptr RegistryFileSink() {
  for (const rex::LogCategoryEntry& entry : rex::GetAllCategories()) {
    if (!entry.logger) {
      continue;
    }
    for (const spdlog::sink_ptr& sink : entry.logger->sinks()) {
      const spdlog::sink_ptr unwrapped = crackdown::UnwrapLogSink(sink);
      if (dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(unwrapped.get()) != nullptr) {
        return unwrapped;
      }
    }
  }
  return {};
}

// One logging session: content filtering, then the session-log outcome when Detailed logging is
// on (retained) and off (the exact active log is discarded, a decoy is left alone).
void TestSessionLifecycle(const std::filesystem::path& dir) {
  std::printf("Detailed retains the session log, Minimal discards only the active log\n");
  rex::LogConfig config;
  config.app_name = "crackdown_session_test";
  config.log_dir = dir.string();
  config.default_level = spdlog::level::info;
  rex::InitLogging(config);
  REXLOG_INFO("Crackdown port 0.4.0 (x86-64, Direct3D 12, ReXGlue SDK 0.10.0)");
  rex::FlushLogging();

  const std::filesystem::path active = crackdown::ActiveLogFile();
  CHECK(!active.empty());
  CHECK(std::filesystem::exists(active));

  spdlog::sink_ptr file = RegistryFileSink();
  auto* rotating = dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(file.get());
  CHECK(rotating != nullptr);
  const size_t max_size = rotating ? rotating->get_max_size() : 0;
  const size_t max_files = rotating ? rotating->get_max_files() : 0;

  SetDetailedLogging(true);
  CHECK(!crackdown::MinimalLogFilterInstalled());
  REXLOG_INFO("ordinary detailed chatter one");
  rex::FlushLogging();
  CHECK(crackdown::ActiveLogFile() == active);

  SetDetailedLogging(false);
  CHECK(crackdown::MinimalLogFilterInstalled());
  REXLOG_INFO("ordinary detailed chatter two");
  REXLOG_WARN("a warning survives");
  REXLOG_ERROR("an error survives");
  REXLOG_INFO("Windows crash reporting enabled for current log.");
  REXLOG_INFO("Graphics card: Test GPU (adapter 0)");
  REXLOG_INFO("Internal render resolution: 75% 960x540 -> 1280x720");
  rex::FlushLogging();

  const std::string log = ReadText(active);
  CHECK(Has(log, "ordinary detailed chatter one"));
  CHECK(!Has(log, "ordinary detailed chatter two"));
  CHECK(Has(log, "a warning survives"));
  CHECK(Has(log, "an error survives"));
  CHECK(Has(log, "Windows crash reporting enabled for current log."));
  CHECK(Has(log, "Graphics card: Test GPU"));
  CHECK(Has(log, "Internal render resolution:"));
  CHECK(rotating && rotating->get_max_size() == max_size && rotating->get_max_files() == max_files);
  file.reset();

  // Detailed on: a clean finalization keeps the log.
  SetDetailedLogging(true);
  crackdown::FinalizeSessionLog();
  CHECK(std::filesystem::exists(active));

  // Detailed off: a clean finalization removes exactly the active log, never the decoy.
  const std::filesystem::path decoy = dir / "crackdown_900.log";
  WriteText(decoy, "decoy\n");
  SetDetailedLogging(false);
  crackdown::FinalizeSessionLog();
  CHECK(!std::filesystem::exists(active));
  CHECK(std::filesystem::exists(decoy));
  CHECK(!crackdown::MinimalLogFilterInstalled());
  CHECK(crackdown::ActiveLogFile().empty());
}

void TestMissingAndRepeatedFinalization() {
  std::printf("a missing active log and a repeated finalization are harmless\n");
  SetDetailedLogging(false);
  CHECK(crackdown::ActiveLogFile().empty());
  crackdown::FinalizeSessionLog();
  crackdown::FinalizeSessionLog();
  CHECK(crackdown::ActiveLogFile().empty());
}

#if defined(_WIN32)

std::wstring SelfPath() {
  wchar_t path[MAX_PATH] = L"";
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

DWORD RunChild(const std::wstring& arguments) {
  std::wstring command = L"\"" + SelfPath() + L"\" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &process)) {
    std::printf("  FAIL could not start child (%lu)\n", GetLastError());
    return 0xFFFFFFFFu;
  }
  DWORD exit_code = 0xFFFFFFFFu;
  if (WaitForSingleObject(process.hProcess, 30000) == WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess, exit_code);
    std::printf("  FAIL the child did not exit\n");
  } else {
    GetExitCodeProcess(process.hProcess, &exit_code);
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return exit_code;
}

bool AnyFile(const std::filesystem::path& dir, const char* prefix, const char* extension) {
  std::error_code ec;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.starts_with(prefix) && entry.path().extension() == extension) {
      return true;
    }
  }
  return false;
}

std::filesystem::path OnlyLog(const std::filesystem::path& dir) {
  std::error_code ec;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".log") {
      return entry.path();
    }
  }
  return {};
}

int RunChildMode(const std::wstring& mode, const std::filesystem::path& dir) {
  rex::LogConfig config;
  config.app_name = "crackdown_session_child";
  config.log_dir = dir.string();
  config.default_level = spdlog::level::info;
  rex::InitLogging(config);
  SetDetailedLogging(false);
  REXLOG_INFO("Crackdown port 0.4.0 (x86-64, Direct3D 12, ReXGlue SDK 0.10.0)");
  REXLOG_WARN("pre-crash warning");
  REXLOG_INFO("Graphics card: Test GPU (adapter 0)");
  crackdown::InstallSessionLogCleanup();

  if (mode == L"clean") {
    return 0;
  }
  if (mode == L"hard-exit") {
    crackdown::FinalizeSessionLog();
    crackdown::FinalizeSessionLog();
    REXLOG_INFO("after finalization");
    rex::FlushLogging();
    std::_Exit(0);
  }
  if (mode == L"stop-title") {
    REXLOG_INFO("Window closing, shutting down...");
    crackdown::FinalizeSessionLog();
    crackdown::FinalizeSessionLog();
    REXLOG_INFO("Title terminated; hard-exiting process.");
    REXLOG_ERROR("XThread::StopTitleThreads: guest threads are still running; exiting");
    REXLOG_ERROR("after finalization");
    rex::FlushLogging();
    std::_Exit(EXIT_FAILURE);
  }
  if (mode == L"hard-exit-detailed") {
    SetDetailedLogging(true);
    REXLOG_INFO("hard exit with detailed logging");
    crackdown::FinalizeSessionLog();
    REXLOG_INFO("after finalization");
    rex::FlushLogging();
    std::_Exit(0);
  }
  if (mode == L"delete-fail") {
    const std::filesystem::path active = crackdown::ActiveLogFile();
    const HANDLE held = CreateFileW(active.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    crackdown::FinalizeSessionLog();
    if (held != INVALID_HANDLE_VALUE) {
      CloseHandle(held);
    }
    return 0;
  }
  if (mode == L"crash") {
    std::error_code ec;
    const std::filesystem::path active = crackdown::ActiveLogFile();
    const std::filesystem::path absolute = std::filesystem::absolute(active, ec);
    crackdown::InstallCrashReporting(Utf8(ec ? active : absolute).c_str());
    rex::FlushLogging();
    RaiseException(0xC0000374u, 0, 0, nullptr);
  }
  return 0;
}

void TestChildProcesses(const std::filesystem::path& root) {
  std::printf("clean, hard-exit, StopTitleThreads, detailed and crash children\n");

  const std::filesystem::path clean = root / "child_clean";
  std::filesystem::create_directories(clean);
  CHECK(RunChild(L"--child clean \"" + clean.wstring() + L"\"") == 0);
  CHECK(!AnyFile(clean, "", ".log"));

  const std::filesystem::path hard_exit = root / "child_hard_exit";
  std::filesystem::create_directories(hard_exit);
  CHECK(RunChild(L"--child hard-exit \"" + hard_exit.wstring() + L"\"") == 0);
  CHECK(!AnyFile(hard_exit, "", ".log"));

  const std::filesystem::path stop_title = root / "child_stop_title";
  std::filesystem::create_directories(stop_title);
  CHECK(RunChild(L"--child stop-title \"" + stop_title.wstring() + L"\"") == 1);
  CHECK(!AnyFile(stop_title, "", ".log"));

  const std::filesystem::path detailed = root / "child_hard_exit_detailed";
  std::filesystem::create_directories(detailed);
  CHECK(RunChild(L"--child hard-exit-detailed \"" + detailed.wstring() + L"\"") == 0);
  const std::filesystem::path detailed_log = OnlyLog(detailed);
  CHECK(!detailed_log.empty());
  const std::string detailed_text = detailed_log.empty() ? std::string() : ReadText(detailed_log);
  CHECK(Has(detailed_text, "hard exit with detailed logging"));
  CHECK(Has(detailed_text, "after finalization"));

  const std::filesystem::path delete_fail = root / "child_delete_fail";
  std::filesystem::create_directories(delete_fail);
  CHECK(RunChild(L"--child delete-fail \"" + delete_fail.wstring() + L"\"") == 0);
  CHECK(AnyFile(delete_fail, "", ".log"));

  const std::filesystem::path crash = root / "child_crash";
  std::filesystem::create_directories(crash);
  CHECK(RunChild(L"--child crash \"" + crash.wstring() + L"\"") == 0xC0000374u);
  const std::filesystem::path log = OnlyLog(crash);
  CHECK(!log.empty());
  const std::string text = log.empty() ? std::string() : ReadText(log);
  CHECK(Has(text, "Crackdown port 0.4.0"));
  CHECK(Has(text, "Graphics card: Test GPU"));
  CHECK(Has(text, "pre-crash warning"));
  CHECK(Has(text, "CRASH REPORT"));
  CHECK(Has(text, "Exception code: 0xC0000374 (STATUS_HEAP_CORRUPTION)"));
  CHECK(!AnyFile(crash, "crash-", ".txt"));
  CHECK(!AnyFile(crash, "", ".dmp"));

  std::error_code ec;
  std::filesystem::remove_all(root / "child_clean", ec);
  std::filesystem::remove_all(root / "child_hard_exit", ec);
  std::filesystem::remove_all(root / "child_stop_title", ec);
  std::filesystem::remove_all(root / "child_hard_exit_detailed", ec);
  std::filesystem::remove_all(root / "child_delete_fail", ec);
  std::filesystem::remove_all(root / "child_crash", ec);
}

#endif

void TestWiring() {
  std::printf("setting, About page and lifecycle wiring\n");
  const std::string filter = ReadSource("log_filter.cpp");
  CHECK(Has(filter, "REXCVAR_DEFINE_BOOL(crackdown_detailed_logging, true"));
  CHECK(Has(filter, "Windows crash reporting enabled for current log."));
  CHECK(Has(filter, "Crackdown port "));

  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "    \"crackdown_detailed_logging\",\n") ||
        Has(settings, "    \"crackdown_detailed_logging\",\r\n"));

  const std::string header = ReadSource("settings.h");
  CHECK(Has(header, "REXCVAR_DECLARE(bool, crackdown_detailed_logging);"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.Heading(\"Diagnostics\")"));
  CHECK(Has(launcher, "ui.Toggle(\"Detailed logging\""));
  CHECK(Has(launcher, "Keeps logs for normal sessions. Crash diagnostics are always preserved."));
  CHECK(Has(launcher, "\"crackdown_detailed_logging\", true);"));
  CHECK(Has(launcher, "Open logs folder"));

  const std::string session = ReadSource("session_log.cpp");
  CHECK(Has(session, "crackdown::ActiveLogFile()") || Has(session, "ActiveLogFile()"));
  CHECK(Has(session, "DetailedLoggingEnabled()"));
  CHECK(Has(session, "CRASH REPORT"));
  CHECK(Has(session, "DetachFileSink()"));
  CHECK(!Has(session, "ShutdownLogging()"));
  CHECK(!Has(session, "*"));

  const std::string app = ReadSource("crackdown_app.h");
  CHECK(Has(app, "crackdown::ApplyDetailedLogging();"));
  CHECK(Has(app, "crackdown::InstallSessionLogCleanup();"));
  CHECK(Has(app, "crackdown::FinalizeSessionLog();"));
  CHECK(Has(app, "crackdown::LoadPlayerSettings("));
  CHECK(!Has(app, "crash-"));
  CHECK(!Has(app, ".dmp"));
  CHECK(!Has(app, "MiniDump"));

  const std::string closing = FunctionBody(app, "void OnClosing(");
  const size_t closing_finalize = closing.find("crackdown::FinalizeSessionLog();");
  const size_t closing_terminate = closing.find("TerminateTitle();");
  CHECK(closing_finalize != std::string::npos);
  CHECK(closing_terminate != std::string::npos);
  CHECK(closing_finalize < closing_terminate);

  const std::string shutdown = FunctionBody(app, "void OnShutdown(");
  CHECK(Has(shutdown, "crackdown::FinalizeSessionLog();"));
}

}  // namespace

int main() {
#if defined(_WIN32)
  int wide_argc = 0;
  LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
  if (wide_argv != nullptr && wide_argc >= 4 && std::wstring_view(wide_argv[1]) == L"--child") {
    const std::wstring mode = wide_argv[2];
    const std::filesystem::path dir = wide_argv[3];
    LocalFree(wide_argv);
    return RunChildMode(mode, dir);
  }
  if (wide_argv != nullptr) {
    LocalFree(wide_argv);
  }
#endif

  std::error_code ec;
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path(ec) / "crackdown_log_filter_tests";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  TestClassification();
  TestSettingValue(dir);
  TestCrashReportIndependent();
  TestSessionLifecycle(dir);
  TestMissingAndRepeatedFinalization();
#if defined(_WIN32)
  TestChildProcesses(dir);
#endif
  TestWiring();

  std::filesystem::remove_all(dir, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
