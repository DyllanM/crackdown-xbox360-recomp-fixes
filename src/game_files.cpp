#include "game_files.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "game_manifest.h"
#include "sha256.h"
#include "xdvdfs.h"

namespace crackdown {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kSpaceMargin = 64ull << 20;
constexpr size_t kChunkBytes = 1 << 20;
constexpr const char* kStagingSuffix = "-install-temp";
constexpr const char* kBackupSuffix = "-install-backup";

constexpr const char* kPhaseCopying = "Copying the game files";
constexpr const char* kPhaseVerifying = "Verifying the copy";
constexpr const char* kPhaseFinishing = "Finishing";

InstallResult Failure(InstallStatus status, std::string detail = {}) {
  return {status, std::move(detail)};
}

// Log text must not depend on the system code page, so paths go through UTF-8.
std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

// ---- Paths ----

fs::path::string_type LowerNative(fs::path::string_type text) {
  for (auto& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<fs::path::value_type>(c - 'A' + 'a');
    }
  }
  return text;
}

std::vector<std::string_view> SplitPath(std::string_view path) {
  std::vector<std::string_view> parts;
  while (!path.empty()) {
    const size_t end = path.find('/');
    if (end != 0) {
      parts.push_back(path.substr(0, end));
    }
    path = end == std::string_view::npos ? std::string_view{} : path.substr(end + 1);
  }
  return parts;
}

fs::path FromPortable(std::string_view path) {
  fs::path out;
  for (const std::string_view part : SplitPath(path)) {
    out /= fs::path(std::string(part));
  }
  return out;
}

bool IsLink(const fs::path& path) {
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    return false;
  }
  // Cloud placeholders are reparse points too but redirect nothing; symlinks and junctions set the name-surrogate bit.
  WIN32_FIND_DATAW data;
  const HANDLE found = FindFirstFileW(path.c_str(), &data);
  if (found == INVALID_HANDLE_VALUE) {
    return true;
  }
  FindClose(found);
  return (data.dwReserved0 & 0x20000000u) != 0;
#else
  std::error_code ec;
  return fs::is_symlink(path, ec);
#endif
}

bool IsAbsent(const fs::path& path) {
  std::error_code ec;
  return fs::symlink_status(path, ec).type() == fs::file_type::not_found;
}

// Recursive deletion must not follow a link, so a link anywhere under `dir` makes the folder unsafe to delete.
bool ContainsLink(const fs::path& dir) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (IsLink(it->path())) {
      return true;
    }
  }
  return static_cast<bool>(ec);
}

bool IsPlainFolder(const fs::path& path) {
  std::error_code ec;
  return !IsLink(path) && fs::is_directory(fs::symlink_status(path, ec)) && !ContainsLink(path);
}

bool SameComponent(const fs::path& a, const fs::path& b) {
#if defined(_WIN32)
  return LowerNative(a.native()) == LowerNative(b.native());
#else
  return a == b;
#endif
}

fs::path Resolved(const fs::path& path) {
  std::error_code ec;
  fs::path absolute = fs::absolute(path, ec);
  fs::path out = fs::weakly_canonical(absolute, ec);
  if (ec) {
    out = absolute;
  }
  out = out.lexically_normal();
  if (!out.has_filename() && out.has_relative_path()) {
    out = out.parent_path();
  }
  return out;
}

bool IsSameOrInside(const fs::path& outer, const fs::path& inner) {
  auto o = outer.begin();
  auto i = inner.begin();
  for (; o != outer.end(); ++o, ++i) {
    if (i == inner.end() || !SameComponent(*o, *i)) {
      return false;
    }
  }
  return true;
}

bool IsInside(const fs::path& root, const fs::path& path) {
  const fs::path relative = path.lexically_relative(root);
  return !relative.empty() && relative != "." && *relative.begin() != "..";
}

std::optional<fs::path> FindEntry(const fs::path& dir, std::string_view name) {
  const fs::path::string_type wanted = LowerNative(fs::path(std::string(name)).native());
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (LowerNative(it->path().filename().native()) == wanted) {
      return it->path();
    }
  }
  return std::nullopt;
}

// `relative` is forward-slash separated; every component is matched ignoring case.
std::optional<fs::path> FindFile(const fs::path& root, std::string_view relative) {
  std::error_code ec;
  const fs::path direct = root / FromPortable(relative);
  if (fs::is_regular_file(direct, ec)) {
    return direct;
  }
  fs::path current = root;
  for (const std::string_view part : SplitPath(relative)) {
    const std::optional<fs::path> next = FindEntry(current, part);
    if (!next) {
      return std::nullopt;
    }
    current = *next;
  }
  if (!fs::is_regular_file(current, ec)) {
    return std::nullopt;
  }
  return current;
}

// ---- Hashing ----

bool HashStream(std::istream& in, uint64_t size, Sha256Digest& digest) {
  Sha256 hash;
  std::vector<char> buffer(kChunkBytes);
  while (size > 0) {
    const size_t want = size < buffer.size() ? size_t(size) : buffer.size();
    in.read(buffer.data(), std::streamsize(want));
    if (in.gcount() != std::streamsize(want)) {
      return false;
    }
    hash.Update(buffer.data(), want);
    size -= want;
  }
  digest = hash.Finish();
  return true;
}

// ---- Install sources ----

struct SourceFile {
  std::string path;  // as stored in the source, forward-slash separated
  uint64_t size = 0;
  bool complete = true;  // all of its data is present
  fs::path real_path;    // folder sources
  uint64_t offset = 0;   // image sources, absolute
};

class Source {
 public:
  virtual ~Source() = default;
  virtual std::optional<SourceFile> Find(std::string_view path) = 0;
  virtual bool Read(const SourceFile& file, uint64_t position, void* out, size_t length) = 0;

  bool Hash(const SourceFile& file, Sha256Digest& digest) {
    Sha256 hash;
    std::vector<char> buffer(kChunkBytes);
    for (uint64_t position = 0; position < file.size;) {
      const size_t want = size_t(std::min<uint64_t>(buffer.size(), file.size - position));
      if (!Read(file, position, buffer.data(), want)) {
        return false;
      }
      hash.Update(buffer.data(), want);
      position += want;
    }
    digest = hash.Finish();
    return true;
  }
};

class ImageSource final : public Source {
 public:
  xdvdfs::Result Open(const fs::path& file) { return image_.Open(file); }

  std::optional<SourceFile> Find(std::string_view path) override {
    const xdvdfs::File* found = image_.Find(path);
    if (!found) {
      return std::nullopt;
    }
    SourceFile file;
    file.path = found->path;
    file.size = found->size;
    file.complete = image_.Contains(*found);
    file.offset = found->offset;
    return file;
  }

  bool Read(const SourceFile& file, uint64_t position, void* out, size_t length) override {
    return position <= file.size && length <= file.size - position &&
           image_.ReadAt(file.offset + position, out, length);
  }

 private:
  xdvdfs::Image image_;
};

class FolderSource final : public Source {
 public:
  explicit FolderSource(fs::path root) : root_(std::move(root)) {}

  std::optional<SourceFile> Find(std::string_view path) override {
    SourceFile file;
    fs::path current = root_;
    for (const std::string_view part : SplitPath(path)) {
      const std::vector<fs::path>& names = Listing(current);
      const fs::path wanted(std::string{part});
      auto match = std::find(names.begin(), names.end(), wanted);
      if (match == names.end()) {
        const fs::path::string_type key = LowerNative(wanted.native());
        match = std::find_if(names.begin(), names.end(),
                             [&](const fs::path& name) { return LowerNative(name.native()) == key; });
      }
      if (match == names.end()) {
        return std::nullopt;
      }
      current /= *match;
      file.path += (file.path.empty() ? "" : "/") + match->string();
    }
    std::error_code ec;
    if (!fs::is_regular_file(current, ec)) {
      return std::nullopt;
    }
    file.size = fs::file_size(current, ec);
    if (ec) {
      return std::nullopt;
    }
    file.real_path = std::move(current);
    return file;
  }

  bool Read(const SourceFile& file, uint64_t position, void* out, size_t length) override {
    if (position > file.size || length > file.size - position) {
      return false;
    }
    if (open_path_ != file.real_path) {
      open_.close();
      open_.clear();
      open_.open(file.real_path, std::ios::binary);
      open_path_ = file.real_path;
    }
    open_.clear();
    open_.seekg(std::streamoff(position));
    open_.read(static_cast<char*>(out), std::streamsize(length));
    return open_.gcount() == std::streamsize(length);
  }

 private:
  const std::vector<fs::path>& Listing(const fs::path& dir) {
    auto [it, added] = listings_.try_emplace(dir);
    if (added) {
      std::error_code ec;
      for (fs::directory_iterator entry(dir, ec), end; !ec && entry != end; entry.increment(ec)) {
        it->second.push_back(entry->path().filename());
      }
    }
    return it->second;
  }

  fs::path root_;
  std::map<fs::path, std::vector<fs::path>> listings_;
  std::ifstream open_;
  fs::path open_path_;
};

// ---- Work folders ----

struct WorkPaths {
  fs::path target;
  fs::path parent;
  fs::path staging;
  fs::path backup;
};

std::optional<WorkPaths> ResolveWorkPaths(const fs::path& game_dir) {
  if (game_dir.empty()) {
    return std::nullopt;
  }
  std::error_code ec;
  WorkPaths paths;
  paths.target = fs::absolute(game_dir, ec).lexically_normal();
  if (ec) {
    return std::nullopt;
  }
  if (!paths.target.has_filename()) {
    paths.target = paths.target.parent_path();
  }
  const fs::path name = paths.target.filename();
  paths.parent = paths.target.parent_path();
  if (name.empty() || name == "." || name == ".." || paths.parent.empty() || paths.parent == paths.target ||
      !fs::is_directory(paths.parent, ec)) {
    return std::nullopt;
  }
  paths.staging = paths.parent / name;
  paths.staging += kStagingSuffix;
  paths.backup = paths.parent / name;
  paths.backup += kBackupSuffix;
  return paths;
}

// The antivirus scanner can hold a freshly written folder for a moment.
bool RenameWithRetry(const fs::path& from, const fs::path& to, const FileOps& ops, std::error_code& last) {
  const int attempts = std::max(1, ops.rename_attempts);
  for (int attempt = 0; attempt < attempts; ++attempt) {
    last.clear();
    if (ops.rename) {
      ops.rename(from, to, last);
    } else {
      fs::rename(from, to, last);
    }
    if (!last) {
      return true;
    }
    if (attempt + 1 < attempts && ops.retry_delay.count() > 0) {
      std::this_thread::sleep_for(ops.retry_delay);
    }
  }
  return false;
}

enum class Removal { kGone, kUnsafe, kFailed };

// Deletes a work folder only when no link is at or under it.
Removal RemoveWorkFolder(const fs::path& dir, const FileOps& ops) {
  if (IsAbsent(dir)) {
    return Removal::kGone;
  }
  if (!IsPlainFolder(dir)) {
    return Removal::kUnsafe;
  }
  std::error_code ec;
  if (ops.remove_all) {
    ops.remove_all(dir, ec);
  } else {
    fs::remove_all(dir, ec);
  }
  return !ec && IsAbsent(dir) ? Removal::kGone : Removal::kFailed;
}

class WorkFolderGuard {
 public:
  WorkFolderGuard(fs::path dir, const FileOps& ops) : dir_(std::move(dir)), ops_(ops) {}
  WorkFolderGuard(const WorkFolderGuard&) = delete;
  WorkFolderGuard& operator=(const WorkFolderGuard&) = delete;
  ~WorkFolderGuard() {
    if (armed_) {
      RemoveWorkFolder(dir_, ops_);
    }
  }
  void Release() { armed_ = false; }

 private:
  fs::path dir_;
  const FileOps& ops_;
  bool armed_ = true;
};

// The previous install survives an interrupted swap as the backup folder; this puts it back.
bool RestoreBackup(const WorkPaths& paths, const GameDescriptor& game, const FileOps& ops, std::error_code& error) {
  std::error_code ec;
  if (IsLink(paths.backup) || !fs::is_directory(paths.backup, ec) || !FindFile(paths.backup, game.xex_name)) {
    return false;
  }
  return RenameWithRetry(paths.backup, paths.target, ops, error);
}

// ---- The installer ----

class Installer {
 public:
  Installer(const fs::path& source, const fs::path& game_dir, const InstallHooks& hooks, const GameDescriptor& game,
            const FileOps& ops)
      : source_path_(source), game_dir_(game_dir), hooks_(hooks), game_(game), ops_(ops) {}

  InstallResult Run() {
    using Step = InstallResult (Installer::*)();
    for (const Step step : {&Installer::ResolveTarget, &Installer::OpenSource, &Installer::CheckOverlap,
                            &Installer::CheckTarget, &Installer::PlanFiles, &Installer::ClearWorkFolders,
                            &Installer::CheckSpace, &Installer::Stage, &Installer::Verify, &Installer::Swap}) {
      if (InstallResult result = (this->*step)(); !result.ok()) {
        return result;
      }
    }
    return {InstallStatus::kOk, notes_};
  }

 private:
  InstallResult ResolveTarget();
  InstallResult OpenSource();
  InstallResult CheckOverlap();
  InstallResult CheckTarget();
  InstallResult PlanFiles();
  InstallResult ClearWorkFolders();
  InstallResult CheckSpace();
  InstallResult Stage();
  InstallResult Verify();
  InstallResult Swap();

  InstallResult CopyOne(const SourceFile& file, const fs::path& dest, uint64_t& done);
  bool Report(const char* phase, uint64_t done) const;

  fs::path source_path_;
  fs::path game_dir_;
  const InstallHooks& hooks_;
  const GameDescriptor& game_;
  const FileOps& ops_;

  WorkPaths paths_;
  std::unique_ptr<Source> source_;
  std::vector<SourceFile> plan_;
  uint64_t total_bytes_ = 0;
  std::vector<char> buffer_;
  std::optional<WorkFolderGuard> staging_guard_;
  std::string notes_;  // a problem that did not fail the install
};

bool Installer::Report(const char* phase, uint64_t done) const {
  return !hooks_.progress || hooks_.progress(InstallProgress{phase, done, total_bytes_});
}

InstallResult Installer::ResolveTarget() {
  if (source_path_.empty()) {
    return Failure(InstallStatus::kNoSource, "no source was chosen");
  }
  const std::optional<WorkPaths> paths = ResolveWorkPaths(game_dir_);
  if (!paths) {
    return Failure(InstallStatus::kNoGameFolder, "unusable game folder '" + Utf8(game_dir_) + "'");
  }
  paths_ = *paths;
  return {};
}

InstallResult Installer::OpenSource() {
  std::error_code ec;
  const fs::file_status status = fs::status(source_path_, ec);
  if (status.type() == fs::file_type::not_found) {
    return Failure(InstallStatus::kNoSource, "'" + Utf8(source_path_) + "' does not exist");
  }
  if (ec) {
    return Failure(InstallStatus::kNotAnInstallSource, "cannot examine the source: " + ec.message());
  }
  if (fs::is_directory(status)) {
    source_ = std::make_unique<FolderSource>(source_path_);
    return {};
  }
  if (!fs::is_regular_file(status)) {
    return Failure(InstallStatus::kNotAnInstallSource, "the source is neither a file nor a folder");
  }

  auto image = std::make_unique<ImageSource>();
  const xdvdfs::Result opened = image->Open(source_path_);
  switch (opened.status) {
    case xdvdfs::Status::kOk:
      source_ = std::move(image);
      return {};
    case xdvdfs::Status::kIoError:
      return Failure(InstallStatus::kIoError, opened.detail);
    case xdvdfs::Status::kTruncated:
      return Failure(InstallStatus::kIncomplete, opened.detail);
    case xdvdfs::Status::kNotAnImage:
    case xdvdfs::Status::kCorrupt:
    case xdvdfs::Status::kUnsafeName:
      break;
  }
  return Failure(InstallStatus::kNotAnInstallSource, opened.detail);
}

InstallResult Installer::CheckOverlap() {
  const fs::path source = Resolved(source_path_);
  for (const fs::path* work : {&paths_.target, &paths_.staging, &paths_.backup}) {
    const fs::path other = Resolved(*work);
    if (IsSameOrInside(other, source) || IsSameOrInside(source, other)) {
      return Failure(InstallStatus::kUnsafeTarget, "the source overlaps '" + Utf8(*work) + "'");
    }
  }
  return {};
}

// The game folder may be missing, empty or hold an earlier install; only then may it be replaced.
InstallResult Installer::CheckTarget() {
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(paths_.target, ec);
  if (status.type() == fs::file_type::not_found) {
    return {};
  }
  if (ec) {
    return Failure(InstallStatus::kIoError, "cannot examine the game folder: " + ec.message());
  }
  if (IsLink(paths_.target)) {
    return Failure(InstallStatus::kUnsafeTarget, "the game folder is a link");
  }
  if (!fs::is_directory(status)) {
    return Failure(InstallStatus::kNoGameFolder, "the game folder path is not a folder");
  }
  const bool empty = fs::is_empty(paths_.target, ec);
  if (ec) {
    return Failure(InstallStatus::kIoError, "cannot read the game folder: " + ec.message());
  }
  if (!empty && !FindFile(paths_.target, game_.xex_name)) {
    return Failure(InstallStatus::kForeignFolder, "the game folder holds other files");
  }
  if (ContainsLink(paths_.target)) {
    return Failure(InstallStatus::kUnsafeTarget, "the game folder contains links");
  }
  return {};
}

// Fails early, before any byte is copied: the right build of the executable, then every required file.
InstallResult Installer::PlanFiles() {
  const std::optional<SourceFile> xex = source_->Find(game_.xex_name);
  if (!xex) {
    return Failure(InstallStatus::kNotAnInstallSource, "the source has no " + game_.xex_name);
  }
  if (!xex->complete) {
    return Failure(InstallStatus::kIncomplete, game_.xex_name + " is cut short");
  }
  if (xex->size != game_.xex_size) {
    return Failure(InstallStatus::kWrongVersion,
                   game_.xex_name + " is " + std::to_string(xex->size) + " bytes, expected " +
                       std::to_string(game_.xex_size));
  }
  Sha256Digest digest;
  if (!source_->Hash(*xex, digest)) {
    return Failure(InstallStatus::kIoError, "cannot read " + game_.xex_name + " from the source");
  }
  if (digest != game_.xex_sha256) {
    return Failure(InstallStatus::kWrongVersion, game_.xex_name + " has SHA-256 " + ToHex(digest));
  }

  plan_.clear();
  total_bytes_ = 0;
  for (const GameDescriptor::File& required : game_.required_files) {
    std::optional<SourceFile> found = source_->Find(required.path);
    if (!found) {
      return Failure(InstallStatus::kIncomplete, "missing " + required.path);
    }
    if (found->size != required.size) {
      return Failure(InstallStatus::kIncomplete, required.path + " is " + std::to_string(found->size) +
                                                     " bytes, expected " + std::to_string(required.size));
    }
    if (!found->complete) {
      return Failure(InstallStatus::kIncomplete, required.path + " is cut short");
    }
    total_bytes_ += found->size;
    plan_.push_back(std::move(*found));
  }
  return {};
}

// Work folders left by an interrupted install are cleared, or the previous install in the backup is restored.
InstallResult Installer::ClearWorkFolders() {
  const auto clear = [&](const fs::path& dir) -> InstallResult {
    switch (RemoveWorkFolder(dir, ops_)) {
      case Removal::kGone:
        return {};
      case Removal::kUnsafe:
        return Failure(InstallStatus::kUnsafeTarget, "'" + Utf8(dir) + "' is or contains a link");
      case Removal::kFailed:
        break;
    }
    return Failure(InstallStatus::kIoError, "cannot clear '" + Utf8(dir) + "'");
  };

  if (InstallResult result = clear(paths_.staging); !result.ok()) {
    return result;
  }
  if (IsAbsent(paths_.backup)) {
    return {};
  }
  if (IsAbsent(paths_.target)) {
    std::error_code error;
    if (RestoreBackup(paths_, game_, ops_, error)) {
      return {};
    }
    if (error) {
      return Failure(InstallStatus::kIoError, "cannot restore '" + Utf8(paths_.backup) + "': " + error.message());
    }
  }
  return clear(paths_.backup);
}

InstallResult Installer::CheckSpace() {
  const uint64_t needed = total_bytes_ + kSpaceMargin;
  std::error_code ec;
  const uintmax_t available =
      ops_.available_space ? ops_.available_space(paths_.parent, ec) : fs::space(paths_.parent, ec).available;
  if (!ec && available < needed) {
    return Failure(InstallStatus::kNoSpace,
                   "needs " + std::to_string(needed) + " bytes, " + std::to_string(available) + " available");
  }
  return {};
}

InstallResult Installer::CopyOne(const SourceFile& file, const fs::path& dest, uint64_t& done) {
  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  if (!out) {
    return Failure(InstallStatus::kIoError, "cannot create '" + Utf8(dest) + "'");
  }
  for (uint64_t position = 0; position < file.size;) {
    const size_t want = size_t(std::min<uint64_t>(buffer_.size(), file.size - position));
    if (!source_->Read(file, position, buffer_.data(), want)) {
      return Failure(InstallStatus::kIoError, "cannot read " + file.path + " from the source");
    }
    out.write(buffer_.data(), std::streamsize(want));
    if (!out) {
      return Failure(InstallStatus::kIoError, "cannot write '" + Utf8(dest) + "'");
    }
    position += want;
    done += want;
    if (!Report(kPhaseCopying, done)) {
      return Failure(InstallStatus::kCancelled);
    }
  }
  out.close();
  if (out.fail()) {
    return Failure(InstallStatus::kIoError, "cannot finish '" + Utf8(dest) + "'");
  }
  return {};
}

InstallResult Installer::Stage() {
  if (!Report(kPhaseCopying, 0)) {
    return Failure(InstallStatus::kCancelled);
  }
  std::error_code ec;
  fs::create_directory(paths_.staging, ec);
  if (ec) {
    return Failure(InstallStatus::kIoError, "cannot create '" + Utf8(paths_.staging) + "': " + ec.message());
  }
  staging_guard_.emplace(paths_.staging, ops_);

  buffer_.resize(kChunkBytes);
  uint64_t done = 0;
  for (const SourceFile& file : plan_) {
    const bool is_xex = LowerNative(fs::path(file.path).native()) == LowerNative(fs::path(game_.xex_name).native());
    const fs::path dest = paths_.staging / FromPortable(is_xex ? game_.xex_name : file.path);
    if (!IsInside(paths_.staging, dest)) {
      return Failure(InstallStatus::kNotAnInstallSource, "'" + file.path + "' would leave the staging folder");
    }
    fs::create_directories(dest.parent_path(), ec);
    if (ec) {
      return Failure(InstallStatus::kIoError, "cannot create the folder for " + file.path + ": " + ec.message());
    }
    if (InstallResult result = CopyOne(file, dest, done); !result.ok()) {
      return result;
    }
  }
  return {};
}

InstallResult Installer::Verify() {
  if (!Report(kPhaseVerifying, total_bytes_)) {
    return Failure(InstallStatus::kCancelled);
  }
  const GameFiles check = CheckGameFiles(paths_.staging, game_);
  switch (check.state) {
    case GameFiles::kReady:
      return {};
    case GameFiles::kUnsupported:
      return Failure(InstallStatus::kWrongVersion, "the copy does not verify: " + check.detail);
    case GameFiles::kMissing:
    case GameFiles::kIncomplete:
      break;
  }
  return Failure(InstallStatus::kIncomplete, "the copy does not verify: " + check.detail);
}

// Two renames: the current folder to the backup, the staging folder to the game folder. Cancelling ends here.
InstallResult Installer::Swap() {
  if (!Report(kPhaseFinishing, total_bytes_)) {
    return Failure(InstallStatus::kCancelled);
  }
  const bool had_previous = !IsAbsent(paths_.target);
  std::error_code error;
  if (had_previous && !RenameWithRetry(paths_.target, paths_.backup, ops_, error)) {
    return Failure(InstallStatus::kIoError,
                   "cannot move '" + Utf8(paths_.target) + "' aside: " + error.message());
  }
  if (!RenameWithRetry(paths_.staging, paths_.target, ops_, error)) {
    std::string detail = "cannot activate '" + Utf8(paths_.target) + "': " + error.message();
    std::error_code restore_error;
    if (had_previous && !RenameWithRetry(paths_.backup, paths_.target, ops_, restore_error)) {
      detail += "; the previous install stays in '" + Utf8(paths_.backup) + "'";
    }
    return Failure(InstallStatus::kIoError, std::move(detail));
  }
  staging_guard_->Release();

  if (had_previous && RemoveWorkFolder(paths_.backup, ops_) != Removal::kGone) {
    notes_ = "the previous install could not be removed from '" + Utf8(paths_.backup) + "'";
  }
  return {};
}

}  // namespace

const GameDescriptor& CrackdownGame() {
  static const GameDescriptor game = [] {
    GameDescriptor descriptor;
    descriptor.xex_name = manifest::kXexName;
    descriptor.xex_size = manifest::kXexSize;
    descriptor.xex_sha256 = manifest::kXexSha256;
    for (const manifest::RequiredFile& file : manifest::kRequiredFiles) {
      descriptor.required_files.push_back({file.path, file.size});
    }
    for (const char* prefix : manifest::kOptionalPrefixes) {
      descriptor.optional_prefixes.push_back(prefix);
    }
    return descriptor;
  }();
  return game;
}

GameFiles CheckGameFiles(const fs::path& game_dir, const GameDescriptor& game) {
  std::error_code ec;
  if (game_dir.empty() || !fs::is_directory(game_dir, ec)) {
    return {GameFiles::kMissing, "no game folder"};
  }
  const std::optional<fs::path> xex = FindFile(game_dir, game.xex_name);
  if (!xex) {
    return {GameFiles::kMissing, "no " + game.xex_name};
  }
  const uintmax_t xex_size = fs::file_size(*xex, ec);
  if (ec) {
    return {GameFiles::kMissing, "cannot read " + game.xex_name};
  }
  if (xex_size != game.xex_size) {
    return {GameFiles::kUnsupported, game.xex_name + " is " + std::to_string(xex_size) + " bytes"};
  }
  std::ifstream in(*xex, std::ios::binary);
  Sha256Digest digest;
  if (!in || !HashStream(in, xex_size, digest)) {
    return {GameFiles::kMissing, "cannot read " + game.xex_name};
  }
  if (digest != game.xex_sha256) {
    return {GameFiles::kUnsupported, game.xex_name + " has SHA-256 " + ToHex(digest)};
  }
  if (xex->filename() != fs::path(game.xex_name)) {
    const fs::path canonical = xex->parent_path() / game.xex_name;
    fs::rename(*xex, canonical, ec);
  }
  for (const GameDescriptor::File& required : game.required_files) {
    const std::optional<fs::path> file = FindFile(game_dir, required.path);
    if (!file) {
      return {GameFiles::kIncomplete, "missing " + required.path};
    }
    const uintmax_t size = fs::file_size(*file, ec);
    if (ec || size != required.size) {
      return {GameFiles::kIncomplete, required.path + " has the wrong size"};
    }
  }
  return {GameFiles::kReady, {}};
}

InstallResult InstallGame(const fs::path& source, const fs::path& game_dir, const InstallHooks& hooks,
                          const GameDescriptor& game, const FileOps& ops) {
  try {
    Installer installer(source, game_dir, hooks, game, ops);
    return installer.Run();
  } catch (const std::exception& error) {
    return Failure(InstallStatus::kIoError, std::string("unexpected error: ") + error.what());
  } catch (...) {
    return Failure(InstallStatus::kIoError, "unexpected error");
  }
}

bool RecoverInterruptedInstall(const fs::path& game_dir, const GameDescriptor& game, const FileOps& ops) {
  try {
    const std::optional<WorkPaths> paths = ResolveWorkPaths(game_dir);
    if (!paths || !IsAbsent(paths->target) || IsAbsent(paths->backup)) {
      return false;
    }
    std::error_code error;
    return RestoreBackup(*paths, game, ops, error);
  } catch (...) {
    return false;
  }
}

InstallMessage DescribeInstall(InstallStatus status) {
  switch (status) {
    case InstallStatus::kOk:
      return {"Game installed", "Press Play to start the game."};
    case InstallStatus::kCancelled:
      return {"Install cancelled", "The game files were not changed."};
    case InstallStatus::kNoSource:
      return {"Nothing selected",
              "Choose the Crackdown disc image (.iso), or the folder that holds the files extracted from the disc."};
    case InstallStatus::kNotAnInstallSource:
      return {"Not a Crackdown disc",
              "The selection is not an Xbox 360 disc image or a folder with the extracted disc files. Choose the "
              ".iso file of the disc, or the folder that contains Default.xex."};
    case InstallStatus::kWrongVersion:
      return {"Different version of the game",
              "This port works with one release of Crackdown, the original game disc. The selected files are a "
              "different version."};
    case InstallStatus::kIncomplete:
      return {"Game files missing",
              "The selected disc image or folder does not hold every file the game needs, or a file is cut short. "
              "Check that the copy or download finished, then try again."};
    case InstallStatus::kNoSpace:
      return {"Not enough disk space",
              "The install needs room for a full copy of the game. Free up space on the drive that holds the game "
              "folder and try again."};
    case InstallStatus::kNoGameFolder:
      return {"No game folder",
              "The folder for the game files cannot be used. Check that its parent folder exists, or choose "
              "another location."};
    case InstallStatus::kForeignFolder:
      return {"Game folder not empty",
              "The game folder already holds other files, so it was not replaced. Choose an empty folder, or move "
              "those files away."};
    case InstallStatus::kUnsafeTarget:
      return {"Cannot use these folders",
              "The game folder or the install work folders contain links, or the source and the game folder "
              "overlap. Nothing was changed. Choose separate, ordinary folders."};
    case InstallStatus::kIoError:
      return {"Could not write the game files",
              "Check that the drive is not full or write-protected and that no other program is using the game "
              "folder, then try again. The previous game files were kept."};
  }
  return {"Install failed", "The game files were not changed."};
}

}  // namespace crackdown
