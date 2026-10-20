// Shutdown lifecycle: a host window close hard-exits without subsystem teardown, while a normal guest exit
// keeps the graceful OnShutdown path. The window calls themselves need the running game, so the decision is
// read from the source.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"

namespace {

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(CRACKDOWN_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string source = text.str();
  for (size_t at = 0; (at = source.find("\r\n", at)) != std::string::npos;) {
    source.erase(at, 1);
  }
  return source;
}

std::string ReadPatch(const char* prefix) {
  const std::string dir = std::string(CRACKDOWN_PATCH_DIR);
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind(prefix, 0) == 0) {
      return check::ReadText(entry.path());
    }
  }
  return {};
}

std::string FunctionBody(const std::string& source, const char* declaration) {
  const size_t start = source.find(declaration);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find("\n  }\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestWindowCloseHardExits() {
  std::printf("window close terminates no resident teardown and hard-exits\n");
  const std::string app = ReadSource("crackdown_app.h");
  CHECK(Has(app, "void OnClosing(rex::ui::UIEvent& e) override"));
  const std::string body = FunctionBody(app, "void OnClosing(");
  CHECK(Has(body, "crackdown::FinalizeSessionLog();"));
  CHECK(Has(body, "rex::FlushLogging();"));
  CHECK(Has(body, "std::_Exit(0);"));
  CHECK(!Has(body, "TerminateTitle"));
  CHECK(!Has(body, "StopTitleThreads"));
  CHECK(!Has(body, "Shutdown("));
}

void TestOrderlyExitKeepsTeardown() {
  std::printf("normal guest exit keeps the graceful teardown\n");
  const std::string app = ReadSource("crackdown_app.h");
  CHECK(Has(app, "void OnShutdown() override"));
  const std::string body = FunctionBody(app, "void OnShutdown(");
  CHECK(Has(body, "crackdown::FinalizeSessionLog();"));
  CHECK(!Has(body, "TerminateTitle"));
  CHECK(!Has(body, "_Exit"));

  const size_t detach = body.find("DetachWindowHost();");
  const size_t input = body.find("input->Shutdown();");
  CHECK(detach != std::string::npos);
  CHECK(input != std::string::npos);
  CHECK(detach < input);
}

void TestInputCallbackDetachesFirst() {
  std::printf("the input-activity callback is cleared before input or runtime teardown\n");
  const std::string host = ReadSource("window_host.cpp");
  const std::string detach = FunctionBody(host, "void Detach()");
  const size_t callback = detach.find("rex::input::SetInputActivityCallback(nullptr);");
  const size_t inactive = detach.find("active_.store(false");
  const size_t restore = detach.find("RestoreCursor();");
  const size_t listener = detach.find("window_->RemoveInputListener(this);");
  CHECK(callback != std::string::npos);
  CHECK(inactive != std::string::npos);
  CHECK(restore != std::string::npos);
  CHECK(listener != std::string::npos);
  CHECK(callback < inactive);
  CHECK(callback < restore);
  CHECK(callback < listener);

  const std::string closing = FunctionBody(host, "void OnClosing(rex::ui::UIEvent&) override");
  CHECK(Has(closing, "rex::input::SetInputActivityCallback(nullptr);"));
  CHECK(Has(closing, "active_.store(false"));
  CHECK(Has(closing, "RestoreCursor();"));
}

void TestEmergencyFallbackRetained() {
  std::printf("an uncooperative guest thread still ends the process after the 2 s stop timeout\n");
  const std::string patch = ReadPatch("0008-");
  CHECK(!patch.empty());
  CHECK(Has(patch, "constexpr auto kStopTimeout = std::chrono::seconds(2);"));
  CHECK(Has(patch, "\"XThread::StopTitleThreads: guest threads are still running; exiting\""));
  CHECK(Has(patch, "std::_Exit(EXIT_FAILURE);"));
  CHECK(Has(patch, "class GuestWait"));
  CHECK(!Has(patch, "TerminateThread("));
  CHECK(!Has(patch, "pthread_cancel("));
}

}  // namespace

int main() {
  TestWindowCloseHardExits();
  TestOrderlyExitKeepsTeardown();
  TestInputCallbackDetachesFirst();
  TestEmergencyFallbackRetained();
  return check::Finish("lifecycle_tests");
}
