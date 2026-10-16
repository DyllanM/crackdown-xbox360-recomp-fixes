#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace check {

inline int& Failures() {
  static int failures = 0;
  return failures;
}

inline int Finish(const char* suite) {
  if (Failures()) {
    std::printf("%s: %d check(s) failed\n", suite, Failures());
    return 1;
  }
  std::printf("%s: all checks passed\n", suite);
  return 0;
}

class TempDir {
 public:
  TempDir() {
    static std::atomic<unsigned> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("crackdown-test-" + std::to_string(stamp) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

inline void WriteText(const std::filesystem::path& file, std::string_view text) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out.write(text.data(), std::streamsize(text.size()));
}

inline std::string ReadText(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace check

#define CHECK(condition)                                                                   \
  do {                                                                                     \
    if (!(condition)) {                                                                    \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);            \
      ++check::Failures();                                                                 \
    }                                                                                      \
  } while (0)
