// Tests for the game-data check and the installer, on synthetic discs and folders built here.
// Run: installer_tests
//      installer_tests --image <iso> --reference <folder> [--folder <dir>] [--workdir <dir>]
// With --image and/or --folder, the real game is installed into a scratch folder and compared file by file
// (size and SHA-256) with the reference folder.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

#include "game_files.h"
#include "sha256.h"
#include "xdvdfs.h"

namespace fs = std::filesystem;
using namespace crackdown;

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

#define CHECK_INSTALL(expression, expected)                                                            \
  do {                                                                                                 \
    ++g_checks;                                                                                        \
    const InstallResult result_ = (expression);                                                        \
    if (result_.status != (expected)) {                                                                \
      ++g_failures;                                                                                    \
      std::printf("  FAIL %s:%d  status %d, wanted %d (%s)\n", __FILE__, __LINE__, int(result_.status), \
                  int(expected), result_.detail.c_str());                                              \
    }                                                                                                  \
  } while (0)

#define CHECK_IMAGE(expression, expected)                                                              \
  do {                                                                                                 \
    ++g_checks;                                                                                        \
    const xdvdfs::Result result_ = (expression);                                                       \
    if (result_.status != (expected)) {                                                                \
      ++g_failures;                                                                                    \
      std::printf("  FAIL %s:%d  image status %d, wanted %d (%s)\n", __FILE__, __LINE__,               \
                  int(result_.status), int(expected), result_.detail.c_str());                         \
    }                                                                                                  \
  } while (0)

void Section(const char* name) {
  std::printf("%s\n", name);
}

using Bytes = std::vector<uint8_t>;
using std::chrono::milliseconds;

// ---- Files and folders ----

void WriteFile(const fs::path& path, const Bytes& data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

Bytes ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

Bytes Pattern(size_t size, uint32_t seed) {
  Bytes data(size);
  uint32_t x = seed * 2654435761u + 1;
  for (uint8_t& b : data) {
    x = x * 1664525u + 1013904223u;
    b = uint8_t(x >> 24);
  }
  return data;
}

Bytes Text(const char* text) {
  return Bytes(text, text + std::strlen(text));
}

class Scratch {
 public:
  explicit Scratch(const fs::path& base) {
    static int counter = 0;
    path_ = base / ("case" + std::to_string(++counter));
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

fs::path g_scratch_base;

std::set<std::string> Listing(const fs::path& root) {
  std::set<std::string> out;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    out.insert(it->path().lexically_relative(root).generic_string());
  }
  return out;
}

// A directory link to `target`: a symbolic link where the system allows one, else a junction, which needs no privilege.
bool MakeDirectoryLink(const fs::path& target, const fs::path& link) {
  std::error_code ec;
  fs::create_directory_symlink(target, link, ec);
  if (!ec) {
    return true;
  }
#if defined(_WIN32)
  const std::string command = "cmd /c mklink /J \"" + link.string() + "\" \"" + target.string() + "\" > nul";
  return std::system(command.c_str()) == 0;
#else
  return false;
#endif
}

void RemoveLink(const fs::path& link) {
  std::error_code ec;
  fs::remove(link, ec);
}

#if defined(_WIN32)
void MarkSparse(const fs::path& path) {
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD returned = 0;
  DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
  CloseHandle(file);
}
#endif

// ---- Synthetic XDVDFS images ----

struct Node {
  std::string name;
  bool dir = false;
  Bytes data;
  std::vector<Node> children;
};

Node MakeFile(std::string name, Bytes data) {
  return {std::move(name), false, std::move(data), {}};
}

Node MakeDir(std::string name, std::vector<Node> children = {}) {
  return {std::move(name), true, {}, std::move(children)};
}

enum class Shape { kBalanced, kRightChain, kLeftChain };

struct ImageOptions {
  uint64_t partition_offset = 0;
  Shape shape = Shape::kBalanced;
  size_t cut = 0;  // bytes dropped from the end of the image
  bool trailing_magic = true;
};

struct BuiltImage {
  Bytes bytes;  // from the start of the partition
  uint64_t partition_offset = 0;
  uint32_t root_sector = 0;
  std::map<std::string, size_t> entry_at;     // path -> offset of its entry within `bytes`
  std::map<std::string, uint32_t> sector_of;  // path -> first sector of a directory or file
};

constexpr uint32_t kSector = 2048;
constexpr size_t kNone = size_t(-1);
constexpr size_t kDescriptorAt = 32 * kSector;
constexpr size_t kRootFieldsAt = kDescriptorAt + 20;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";

void PutLe16(Bytes& b, size_t at, uint16_t v) {
  b[at] = uint8_t(v);
  b[at + 1] = uint8_t(v >> 8);
}

void PutLe32(Bytes& b, size_t at, uint32_t v) {
  PutLe16(b, at, uint16_t(v));
  PutLe16(b, at + 2, uint16_t(v >> 16));
}

std::string Lower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
  }
  return text;
}

struct Flat {
  const Node* node;
  std::string path;
  std::vector<size_t> children;
  uint32_t sector = 0;
  uint32_t size = 0;
};

size_t Flatten(const Node& node, const std::string& path, std::vector<Flat>& flat) {
  const size_t index = flat.size();
  flat.push_back({&node, path, {}, 0, 0});
  for (const Node& child : node.children) {
    const size_t child_index = Flatten(child, path.empty() ? child.name : path + "/" + child.name, flat);
    flat[index].children.push_back(child_index);
  }
  return index;
}

struct Linked {
  size_t flat_index;
  size_t left = kNone;
  size_t right = kNone;
};

size_t LinkBalanced(const std::vector<size_t>& sorted, size_t lo, size_t hi, std::vector<Linked>& out) {
  if (lo >= hi) {
    return kNone;
  }
  const size_t mid = lo + (hi - lo) / 2;
  const size_t at = out.size();
  out.push_back({sorted[mid]});
  const size_t left = LinkBalanced(sorted, lo, mid, out);
  const size_t right = LinkBalanced(sorted, mid + 1, hi, out);
  out[at].left = left;
  out[at].right = right;
  return at;
}

// The entries of one directory in storage order (the tree root first), linked as the shape asks.
std::vector<Linked> LinkDirectory(const std::vector<Flat>& flat, const Flat& dir, Shape shape) {
  std::vector<std::pair<std::string, size_t>> keyed;
  for (const size_t child : dir.children) {
    keyed.emplace_back(Lower(flat[child].node->name), child);
  }
  std::sort(keyed.begin(), keyed.end(), [&](const auto& a, const auto& b) {
    return a.first != b.first ? a.first < b.first : flat[a.second].node->name < flat[b.second].node->name;
  });
  std::vector<size_t> sorted;
  for (const auto& entry : keyed) {
    sorted.push_back(entry.second);
  }
  std::vector<Linked> out;
  switch (shape) {
    case Shape::kBalanced:
      LinkBalanced(sorted, 0, sorted.size(), out);
      break;
    case Shape::kRightChain:
      for (size_t i = 0; i < sorted.size(); ++i) {
        out.push_back({sorted[i], kNone, i + 1 < sorted.size() ? i + 1 : kNone});
      }
      break;
    case Shape::kLeftChain:
      for (size_t i = 0; i < sorted.size(); ++i) {
        out.push_back({sorted[sorted.size() - 1 - i], i + 1 < sorted.size() ? i + 1 : kNone, kNone});
      }
      break;
  }
  return out;
}

size_t EntryBytes(const std::string& name) {
  return (14 + name.size() + 3) & ~size_t(3);
}

// Entry offsets within a directory; an entry never straddles a sector.
std::vector<size_t> PlaceEntries(const std::vector<Flat>& flat, const std::vector<Linked>& linked, size_t& total) {
  std::vector<size_t> offsets;
  size_t at = 0;
  for (const Linked& item : linked) {
    const size_t bytes = EntryBytes(flat[item.flat_index].node->name);
    if (at % kSector + bytes > kSector) {
      at = (at + kSector - 1) / kSector * kSector;
    }
    offsets.push_back(at);
    at += bytes;
  }
  total = std::max<size_t>(kSector, (at + kSector - 1) / kSector * kSector);
  return offsets;
}

BuiltImage BuildImage(const std::vector<Node>& top, const ImageOptions& options = {}) {
  const Node root = MakeDir("", top);
  std::vector<Flat> flat;
  Flatten(root, "", flat);

  std::vector<std::vector<Linked>> links(flat.size());
  std::vector<std::vector<size_t>> offsets(flat.size());
  for (size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].node->dir) {
      links[i] = LinkDirectory(flat, flat[i], options.shape);
      size_t total = 0;
      offsets[i] = PlaceEntries(flat, links[i], total);
      flat[i].size = uint32_t(total);
      if (total > 0x3FFFC) {
        std::printf("  test bug: directory '%s' is too large\n", flat[i].path.c_str());
        std::abort();
      }
    }
  }

  uint32_t next = 33;
  for (Flat& item : flat) {
    if (item.node->dir) {
      item.sector = next;
      next += item.size / kSector;
    }
  }
  for (Flat& item : flat) {
    if (!item.node->dir) {
      item.size = uint32_t(item.node->data.size());
      item.sector = item.size == 0 ? 0 : next;
      next += (item.size + kSector - 1) / kSector;
    }
  }

  BuiltImage image;
  image.partition_offset = options.partition_offset;
  image.root_sector = flat[0].sector;
  image.bytes.assign(size_t(next) * kSector, 0);
  std::memcpy(&image.bytes[kDescriptorAt], kMagic, 20);
  PutLe32(image.bytes, kRootFieldsAt, flat[0].sector);
  PutLe32(image.bytes, kRootFieldsAt + 4, flat[0].size);
  if (options.trailing_magic) {
    std::memcpy(&image.bytes[kDescriptorAt + 0x7EC], kMagic, 20);
  }

  for (size_t i = 0; i < flat.size(); ++i) {
    const Flat& item = flat[i];
    image.sector_of[item.path] = item.sector;
    if (!item.node->dir) {
      if (item.size > 0) {
        std::memcpy(&image.bytes[size_t(item.sector) * kSector], item.node->data.data(), item.size);
      }
      continue;
    }
    const size_t base = size_t(item.sector) * kSector;
    std::memset(&image.bytes[base], 0xFF, item.size);
    for (size_t k = 0; k < links[i].size(); ++k) {
      const Flat& entry = flat[links[i][k].flat_index];
      const size_t at = base + offsets[i][k];
      const auto child_offset = [&](size_t position) {
        return position == kNone ? uint16_t(0) : uint16_t(offsets[i][position] / 4);
      };
      PutLe16(image.bytes, at, child_offset(links[i][k].left));
      PutLe16(image.bytes, at + 2, child_offset(links[i][k].right));
      PutLe32(image.bytes, at + 4, entry.sector);
      PutLe32(image.bytes, at + 8, entry.size);
      image.bytes[at + 12] = entry.node->dir ? 0x10 : 0x20;
      image.bytes[at + 13] = uint8_t(entry.node->name.size());
      std::memcpy(&image.bytes[at + 14], entry.node->name.data(), entry.node->name.size());
      image.entry_at[entry.path] = at;
    }
  }
  image.bytes.resize(image.bytes.size() - std::min(options.cut, image.bytes.size()));
  return image;
}

void WriteImage(const fs::path& path, const BuiltImage& image) {
  fs::create_directories(path.parent_path());
  {
    std::ofstream create(path, std::ios::binary | std::ios::trunc);
  }
#if defined(_WIN32)
  if (image.partition_offset > 0) {
    MarkSparse(path);
  }
#endif
  std::error_code ec;
  fs::resize_file(path, image.partition_offset + image.bytes.size(), ec);
  std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
  out.seekp(std::streamoff(image.partition_offset));
  out.write(reinterpret_cast<const char*>(image.bytes.data()), std::streamsize(image.bytes.size()));
}

xdvdfs::Result OpenImage(const BuiltImage& built, xdvdfs::Image& image, const fs::path& dir) {
  const fs::path path = dir / "image.iso";
  WriteImage(path, built);
  return image.Open(path);
}

std::set<std::string> FilePaths(const xdvdfs::Image& image) {
  std::set<std::string> out;
  for (const xdvdfs::File& file : image.files()) {
    out.insert(file.path);
  }
  return out;
}

bool DataMatches(xdvdfs::Image& image, const std::string& path, const Bytes& expected) {
  const xdvdfs::File* file = image.Find(path);
  if (!file || file->size != expected.size() || !image.Contains(*file)) {
    return false;
  }
  Bytes data(expected.size());
  return image.ReadAt(file->offset, data.data(), data.size()) && data == expected;
}

// ---- The synthetic game ----

constexpr size_t kBigSize = (5u << 19) + 17;  // 2.5 MiB and a bit: three copy chunks
constexpr size_t kXexSize = 700;
const std::string kCopying = "Copying the game files";
const std::string kVerifying = "Verifying the copy";
const std::string kFinishing = "Finishing";

Bytes TestXex() {
  return Pattern(kXexSize, 1);
}

std::vector<Node> GameTree(const Bytes& xex) {
  return {
      MakeFile("Default.xex", xex),
      MakeDir("AUDIO", {MakeFile("a.bin", Pattern(3000, 2)), MakeDir("Deep", {MakeFile("b.bin", Pattern(10, 3))})}),
      MakeDir("Local", {MakeDir("en-US", {MakeFile("Strings.str", Pattern(5000, 4))})}),
      MakeDir("Big", {MakeFile("big.bin", Pattern(kBigSize, 5))}),
      MakeFile("empty.txt", {}),
      MakeDir("$SystemUpdate", {MakeFile("su123", Pattern(4096, 6))}),
      MakeDir("Extra", {MakeFile("ignored.bin", Pattern(100, 7))}),
  };
}

GameDescriptor TinyGame(const Bytes& xex) {
  GameDescriptor game;
  game.xex_name = "default.xex";
  game.xex_size = xex.size();
  Sha256 hash;
  hash.Update(xex.data(), xex.size());
  game.xex_sha256 = hash.Finish();
  game.required_files = {{"default.xex", xex.size()}, {"Audio/a.bin", 3000}, {"Audio/Deep/b.bin", 10},
                         {"Local/en-US/Strings.str", 5000}, {"Big/big.bin", kBigSize}, {"empty.txt", 0}};
  game.optional_prefixes = {"$SystemUpdate/"};
  return game;
}

uint64_t TotalBytes(const GameDescriptor& game) {
  uint64_t total = 0;
  for (const GameDescriptor::File& file : game.required_files) {
    total += file.size;
  }
  return total;
}

// What an install of GameTree leaves, with the casing of the image.
const std::set<std::string> kInstalledListing = {
    "AUDIO",         "AUDIO/Deep",       "AUDIO/Deep/b.bin", "AUDIO/a.bin", "Big",       "Big/big.bin",
    "Default.xex",   "Local",            "Local/en-US",      "Local/en-US/Strings.str", "empty.txt"};

void WriteTree(const fs::path& root, const std::vector<Node>& nodes) {
  for (const Node& node : nodes) {
    if (node.dir) {
      fs::create_directories(root / node.name);
      WriteTree(root / node.name, node.children);
    } else {
      WriteFile(root / node.name, node.data);
    }
  }
}

std::vector<Node> LowerCased(std::vector<Node> nodes) {
  for (Node& node : nodes) {
    node.name = Lower(node.name);
    node.children = LowerCased(std::move(node.children));
  }
  return nodes;
}

// A scratch area with a game folder whose parent exists, the synthetic disc, and the descriptor it matches.
struct Site {
  Scratch scratch{g_scratch_base};
  fs::path parent = scratch.path() / "program";
  fs::path game = parent / "game";
  fs::path staging = parent / "game-install-temp";
  fs::path backup = parent / "game-install-backup";
  fs::path image = scratch.path() / "disc.iso";
  fs::path folder = scratch.path() / "disc";
  Bytes xex = TestXex();
  GameDescriptor descriptor = TinyGame(xex);

  Site() { fs::create_directories(parent); }

  BuiltImage MakeImage(const ImageOptions& options = {}) {
    BuiltImage built = BuildImage(GameTree(xex), options);
    WriteImage(image, built);
    return built;
  }
  void MakeFolder(bool lower_case = false) {
    WriteTree(folder, lower_case ? LowerCased(GameTree(xex)) : GameTree(xex));
  }
  InstallResult Install(const fs::path& source, const InstallHooks& hooks = {}, const FileOps& ops = {}) {
    return InstallGame(source, game, hooks, descriptor, ops);
  }
};

// An installed game with markers, so changes to it are visible. Its executable is not the supported one.
void MakeOldInstall(const fs::path& game) {
  WriteFile(game / "default.xex", Bytes{1, 2, 3});
  WriteFile(game / "data" / "old.bin", Bytes{9, 9, 9});
  WriteFile(game / "marker.txt", Text("old"));
}

bool OldIntact(const fs::path& game) {
  return ReadFile(game / "marker.txt") == Text("old") && ReadFile(game / "data" / "old.bin") == Bytes{9, 9, 9} &&
         ReadFile(game / "default.xex") == Bytes{1, 2, 3};
}

bool NoLeftovers(const Site& site) {
  return !fs::exists(site.staging) && !fs::exists(site.backup);
}

// The installed files keep the casing of their source, so a folder source with lower-case names needs lower-case paths
// on a case-sensitive file system.
bool InstalledFromTree(const fs::path& game, bool lower_case = false) {
  auto at = [&](std::initializer_list<const char*> parts) {
    fs::path path = game;
    for (const char* part : parts) {
      path /= lower_case ? Lower(part) : std::string(part);
    }
    return path;
  };
  return ReadFile(at({"AUDIO", "a.bin"})) == Pattern(3000, 2) &&
         ReadFile(at({"AUDIO", "Deep", "b.bin"})) == Pattern(10, 3) &&
         ReadFile(at({"Local", "en-US", "Strings.str"})) == Pattern(5000, 4) &&
         ReadFile(at({"Big", "big.bin"})) == Pattern(kBigSize, 5) && fs::file_size(at({"empty.txt"})) == 0;
}

struct Recorder {
  std::vector<InstallProgress> calls;
  InstallHooks hooks() {
    InstallHooks hooks;
    hooks.progress = [this](const InstallProgress& progress) {
      calls.push_back(progress);
      return true;
    };
    return hooks;
  }
};

FileOps FailingRenames(std::function<bool(const fs::path&, const fs::path&)> fails, int* calls = nullptr) {
  FileOps ops;
  ops.retry_delay = milliseconds(0);
  ops.rename = [fails = std::move(fails), calls](const fs::path& from, const fs::path& to, std::error_code& ec) {
    if (calls) {
      ++*calls;
    }
    if (fails(from, to)) {
      ec = std::make_error_code(std::errc::permission_denied);
      return;
    }
    fs::rename(from, to, ec);
  };
  return ops;
}

bool IsStagingToTarget(const fs::path& from, const fs::path& to) {
  return from.filename() == "game-install-temp" && to.filename() == "game";
}

bool IsTargetToBackup(const fs::path& from, const fs::path& to) {
  return from.filename() == "game" && to.filename() == "game-install-backup";
}

bool IsBackupToTarget(const fs::path& from, const fs::path& to) {
  return from.filename() == "game-install-backup" && to.filename() == "game";
}

// ---- SHA-256 over files ----

std::string HashFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  Sha256 hash;
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    hash.Update(buffer.data(), size_t(in.gcount()));
  }
  return ToHex(hash.Finish());
}

// ---- XDVDFS reader ----

const std::set<std::string> kUnsafeNames = {
    "..",         ".",       "a\\b",     "a/b",       "C:evil",   "C:\\evil", "\\\\srv\\share", "/abs",
    "NUL",        "com1.txt", "con",     "Aux.dat",   "file.",    "file ",    "x:stream",       "a*b",
    "a?b",        "a\"b",    "a<b",      "a|b",       "\001ctl",  "caf\xE9",  "CONOUT$",        "lpt9",
    "CON .txt",   "COM1.tar.gz", "prn",  "CONIN$",    "~\x7F"};

void TestSafeNames() {
  Section("entry names: one printable-ASCII component, no device names, no trailing dot or space");
  for (const std::string& name : kUnsafeNames) {
    if (xdvdfs::IsSafeName(name)) {
      std::printf("  accepted: %s\n", name.c_str());
    }
    CHECK(!xdvdfs::IsSafeName(name));
  }
  CHECK(!xdvdfs::IsSafeName(""));
  for (const char* name : {"Default.xex", "$SystemUpdate", "DonDomingoGarcia_ INTRO.bik", "con1", "a.b.c", "~tmp",
                           "a b", "COM0", "LPT10", "...x", "Crackdown_Font.dds", "su20076000_00000000"}) {
    if (!xdvdfs::IsSafeName(name)) {
      std::printf("  rejected: %s\n", name);
    }
    CHECK(xdvdfs::IsSafeName(name));
  }
  CHECK(xdvdfs::IsSafeName(std::string(255, 'a')));
}

void TestPartitionOffsets() {
  Section("the volume is found at every known partition offset, with the same files");
  Scratch scratch(g_scratch_base);
  for (const uint64_t offset : {uint64_t(0), uint64_t(0xFD90000), uint64_t(0x2090000), uint64_t(0x18300000)}) {
    ImageOptions options;
    options.partition_offset = offset;
    xdvdfs::Image image;
    CHECK_IMAGE(OpenImage(BuildImage(GameTree(TestXex()), options), image, scratch.path()), xdvdfs::Status::kOk);
    CHECK(image.partition_offset() == offset);
    CHECK(image.files().size() == 8);
    CHECK(DataMatches(image, "Default.xex", TestXex()));
    CHECK(DataMatches(image, "AUDIO/Deep/b.bin", Pattern(10, 3)));
    CHECK(DataMatches(image, "Big/big.bin", Pattern(kBigSize, 5)));
    CHECK(DataMatches(image, "empty.txt", {}));
  }
}

void TestTreeShapes() {
  Section("balanced and degenerate directory trees, nested directories and long names");
  Scratch scratch(g_scratch_base);
  const std::string long_name(255, 'n');
  std::vector<Node> many;
  for (int i = 0; i < 3000; ++i) {
    many.push_back(MakeFile("file" + std::to_string(i), Pattern(3, uint32_t(i))));
  }
  std::vector<Node> top = GameTree(TestXex());
  top.push_back(MakeDir("many", many));
  top.push_back(MakeDir("long", {MakeFile(long_name, Pattern(50, 9)), MakeFile("x", Pattern(5, 1))}));

  std::set<std::string> expected;
  for (const Node& n : GameTree(TestXex())) {
    if (!n.dir) {
      expected.insert(n.name);
    }
  }
  expected.insert({"AUDIO/a.bin", "AUDIO/Deep/b.bin", "Local/en-US/Strings.str", "Big/big.bin",
                   "$SystemUpdate/su123", "Extra/ignored.bin", "long/" + long_name, "long/x"});
  for (int i = 0; i < 3000; ++i) {
    expected.insert("many/file" + std::to_string(i));
  }

  for (const Shape shape : {Shape::kBalanced, Shape::kRightChain, Shape::kLeftChain}) {
    ImageOptions options;
    options.shape = shape;
    xdvdfs::Image image;
    CHECK_IMAGE(OpenImage(BuildImage(top, options), image, scratch.path()), xdvdfs::Status::kOk);
    CHECK(FilePaths(image) == expected);
    CHECK(DataMatches(image, "many/file2999", Pattern(3, 2999)));
    CHECK(DataMatches(image, "many/file0", Pattern(3, 0)));
    CHECK(DataMatches(image, "long/" + long_name, Pattern(50, 9)));
    CHECK(DataMatches(image, "Local/en-US/Strings.str", Pattern(5000, 4)));
  }
}

void TestEmptyAndLookup() {
  Section("empty directories and files, and case-insensitive lookup");
  Scratch scratch(g_scratch_base);
  xdvdfs::Image image;
  CHECK_IMAGE(OpenImage(BuildImage({MakeDir("empty"), MakeFile("Zero.bin", {}), MakeDir("d", {MakeDir("inner")}),
                                    MakeFile("Real.bin", Pattern(10, 1))}),
                        image, scratch.path()),
              xdvdfs::Status::kOk);
  CHECK(FilePaths(image) == (std::set<std::string>{"Zero.bin", "Real.bin"}));
  CHECK(image.Find("zero.BIN") != nullptr);
  CHECK(image.Find("ZERO.bin")->path == "Zero.bin");
  CHECK(image.Find("real.bin") != nullptr);
  CHECK(image.Find("missing.bin") == nullptr);
  CHECK(image.Find("empty") == nullptr);
  CHECK(image.Find("d/inner") == nullptr);
  CHECK(image.Find("Real.bin/") == nullptr);
  CHECK(image.Find("../Real.bin") == nullptr);
  CHECK(image.Find("") == nullptr);
  CHECK(image.Contains(*image.Find("Zero.bin")));

  BuiltImage built = BuildImage({MakeDir("empty"), MakeFile("a", Pattern(5, 1))});
  PutLe32(built.bytes, built.entry_at["empty"] + 8, 0);
  PutLe32(built.bytes, built.entry_at["empty"] + 4, 0);
  xdvdfs::Image zero_sized;
  CHECK_IMAGE(OpenImage(built, zero_sized, scratch.path()), xdvdfs::Status::kOk);
  CHECK(FilePaths(zero_sized) == std::set<std::string>{"a"});
}

void TestNotAnImage() {
  Section("files that are not disc images are rejected without reading further");
  Scratch scratch(g_scratch_base);
  xdvdfs::Image image;
  WriteFile(scratch.path() / "garbage.bin", Pattern(200000, 5));
  WriteFile(scratch.path() / "tiny.bin", Pattern(100, 5));
  WriteFile(scratch.path() / "empty.bin", {});
  WriteFile(scratch.path() / "zeros.bin", Bytes(40 * kSector, 0));
  CHECK_IMAGE(image.Open(scratch.path() / "garbage.bin"), xdvdfs::Status::kNotAnImage);
  CHECK_IMAGE(image.Open(scratch.path() / "tiny.bin"), xdvdfs::Status::kNotAnImage);
  CHECK_IMAGE(image.Open(scratch.path() / "empty.bin"), xdvdfs::Status::kNotAnImage);
  CHECK_IMAGE(image.Open(scratch.path() / "zeros.bin"), xdvdfs::Status::kNotAnImage);
  CHECK_IMAGE(image.Open(scratch.path() / "missing.bin"), xdvdfs::Status::kIoError);
  CHECK_IMAGE(image.Open(scratch.path()), xdvdfs::Status::kIoError);
  CHECK(image.files().empty());

  ImageOptions no_tail;
  no_tail.trailing_magic = false;
  CHECK_IMAGE(OpenImage(BuildImage(GameTree(TestXex()), no_tail), image, scratch.path()), xdvdfs::Status::kNotAnImage);

  BuiltImage bad_head = BuildImage(GameTree(TestXex()));
  bad_head.bytes[kDescriptorAt + 3] ^= 0x20;
  CHECK_IMAGE(OpenImage(bad_head, image, scratch.path()), xdvdfs::Status::kNotAnImage);

  BuiltImage shifted = BuildImage(GameTree(TestXex()));
  shifted.partition_offset = 0x1000;
  CHECK_IMAGE(OpenImage(shifted, image, scratch.path()), xdvdfs::Status::kNotAnImage);
}

void TestTruncatedImages() {
  Section("truncated images and out-of-bounds sectors and sizes");
  Scratch scratch(g_scratch_base);
  xdvdfs::Image image;

  ImageOptions cut;
  cut.cut = kSector;
  CHECK_IMAGE(OpenImage(BuildImage(GameTree(TestXex()), cut), image, scratch.path()), xdvdfs::Status::kOk);
  const xdvdfs::File* last = image.Find("Extra/ignored.bin");
  CHECK(last != nullptr && !image.Contains(*last));
  CHECK(!image.ReadAt(last->offset, Bytes(100).data(), 100));
  CHECK(image.Contains(*image.Find("Default.xex")));
  CHECK(!image.ReadAt(image.image_size() - 10, Bytes(11).data(), 11));
  CHECK(!image.ReadAt(image.image_size() + 1, Bytes(1).data(), 1));

  BuiltImage root_far = BuildImage(GameTree(TestXex()));
  PutLe32(root_far.bytes, kRootFieldsAt, 0x7FFFFFFF);
  CHECK_IMAGE(OpenImage(root_far, image, scratch.path()), xdvdfs::Status::kTruncated);

  BuiltImage root_big = BuildImage(GameTree(TestXex()));
  PutLe32(root_big.bytes, kRootFieldsAt + 4, 0xFFFFFFFFu);
  CHECK_IMAGE(OpenImage(root_big, image, scratch.path()), xdvdfs::Status::kTruncated);

  BuiltImage sub_far = BuildImage(GameTree(TestXex()));
  PutLe32(sub_far.bytes, sub_far.entry_at["Big"] + 4, 0x7FFFFFFF);
  CHECK_IMAGE(OpenImage(sub_far, image, scratch.path()), xdvdfs::Status::kTruncated);

  BuiltImage sub_big = BuildImage(GameTree(TestXex()));
  PutLe32(sub_big.bytes, sub_big.entry_at["Big"] + 8, 0xFFFFFFFFu);
  CHECK_IMAGE(OpenImage(sub_big, image, scratch.path()), xdvdfs::Status::kTruncated);

  BuiltImage file_far = BuildImage(GameTree(TestXex()));
  PutLe32(file_far.bytes, file_far.entry_at["Default.xex"] + 4, 0x7FFFFFFF);
  CHECK_IMAGE(OpenImage(file_far, image, scratch.path()), xdvdfs::Status::kOk);
  CHECK(!image.Contains(*image.Find("Default.xex")));

  BuiltImage file_big = BuildImage(GameTree(TestXex()));
  PutLe32(file_big.bytes, file_big.entry_at["Default.xex"] + 8, 0xFFFFFFFFu);
  CHECK_IMAGE(OpenImage(file_big, image, scratch.path()), xdvdfs::Status::kOk);
  CHECK(!image.Contains(*image.Find("Default.xex")));

  BuiltImage too_much = BuildImage(GameTree(TestXex()));
  too_much.bytes.resize(20u << 20);
  PutLe32(too_much.bytes, kRootFieldsAt + 4, 17u << 20);
  CHECK_IMAGE(OpenImage(too_much, image, scratch.path()), xdvdfs::Status::kCorrupt);
}

void TestMalformedEntries() {
  Section("malformed directory entries and loops in the entry tree");
  Scratch scratch(g_scratch_base);
  xdvdfs::Image image;
  const std::vector<Node> three = {MakeFile("a", Pattern(5, 1)), MakeFile("b", Pattern(5, 2)),
                                   MakeFile("c", Pattern(5, 3))};
  // Balanced: b is the root at offset 0, a at 16, c at 32 (each entry takes 16 bytes).
  {
    BuiltImage ok = BuildImage(three);
    CHECK(ok.entry_at["b"] == size_t(ok.root_sector) * kSector);
    CHECK_IMAGE(OpenImage(ok, image, scratch.path()), xdvdfs::Status::kOk);
    CHECK(image.files().size() == 3);
  }
  struct Case {
    const char* what;
    std::function<void(BuiltImage&)> tamper;
  };
  const std::vector<Case> cases = {
      {"an entry that is its own left child", [](BuiltImage& b) { PutLe16(b.bytes, b.entry_at["a"], 4); }},
      {"an entry reached from two parents",
       [](BuiltImage& b) { PutLe16(b.bytes, b.entry_at["c"], uint16_t((b.entry_at["a"] % kSector) / 4)); }},
      {"a right child that is a sibling reached twice",
       [](BuiltImage& b) { PutLe16(b.bytes, b.entry_at["a"] + 2, uint16_t((b.entry_at["c"] % kSector) / 4)); }},
      {"a child offset beyond the directory", [](BuiltImage& b) { PutLe16(b.bytes, b.entry_at["b"] + 2, 0x7000); }},
      {"an entry with no name", [](BuiltImage& b) { b.bytes[b.entry_at["a"] + 13] = 0; }},
      {"a directory shorter than its entries", [](BuiltImage& b) { PutLe32(b.bytes, kRootFieldsAt + 4, 40); }},
  };
  for (const Case& c : cases) {
    BuiltImage built = BuildImage(three);
    c.tamper(built);
    const xdvdfs::Result result = OpenImage(built, image, scratch.path());
    if (result.ok()) {
      std::printf("  not rejected: %s\n", c.what);
    }
    CHECK(result.status == xdvdfs::Status::kCorrupt);
    CHECK(image.files().empty());
  }

  BuiltImage name_overrun = BuildImage(three);
  PutLe32(name_overrun.bytes, kRootFieldsAt + 4, 47);
  name_overrun.bytes[name_overrun.entry_at["c"] + 13] = 200;
  CHECK_IMAGE(OpenImage(name_overrun, image, scratch.path()), xdvdfs::Status::kCorrupt);
}

void TestDirectoryLoops() {
  Section("directories that point back at a directory already read");
  Scratch scratch(g_scratch_base);
  xdvdfs::Image image;
  const std::vector<Node> tree = {MakeDir("d1", {MakeDir("e", {MakeDir("k", {MakeFile("f", Pattern(5, 1))})})}),
                                  MakeDir("d2", {MakeFile("g", Pattern(5, 2))}), MakeFile("h", Pattern(5, 3))};
  CHECK_IMAGE(OpenImage(BuildImage(tree), image, scratch.path()), xdvdfs::Status::kOk);
  CHECK(FilePaths(image) == (std::set<std::string>{"d1/e/k/f", "d2/g", "h"}));

  struct Case {
    const char* what;
    std::function<void(BuiltImage&)> tamper;
  };
  const std::vector<Case> cases = {
      {"a directory that lists its parent as a child",
       [](BuiltImage& b) { PutLe32(b.bytes, b.entry_at["d1/e"] + 4, b.sector_of["d1"]); }},
      {"a directory that is the root",
       [](BuiltImage& b) { PutLe32(b.bytes, b.entry_at["d1"] + 4, b.root_sector); }},
      {"a nested directory that is the root",
       [](BuiltImage& b) { PutLe32(b.bytes, b.entry_at["d1/e"] + 4, b.root_sector); }},
      {"a grandchild that points at its grandparent",
       [](BuiltImage& b) { PutLe32(b.bytes, b.entry_at["d1/e/k"] + 4, b.sector_of["d1"]); }},
      {"two directories sharing one sector",
       [](BuiltImage& b) { PutLe32(b.bytes, b.entry_at["d2"] + 4, b.sector_of["d1"]); }},
  };
  for (const Case& c : cases) {
    BuiltImage built = BuildImage(tree);
    c.tamper(built);
    const xdvdfs::Result result = OpenImage(built, image, scratch.path());
    if (result.ok()) {
      std::printf("  not rejected: %s\n", c.what);
    }
    CHECK(result.status == xdvdfs::Status::kCorrupt);
  }
}

std::vector<Node> NestedDirectories(size_t depth, const std::string& name = "d") {
  Node node = MakeFile("leaf.txt", Pattern(4, 1));
  std::vector<Node> chain = {node};
  for (size_t i = 0; i < depth; ++i) {
    chain = {MakeDir(name + std::to_string(i), std::move(chain))};
  }
  return chain;
}

std::vector<Node> ManyFiles(size_t directories, size_t per_directory) {
  std::vector<Node> top;
  for (size_t d = 0; d < directories; ++d) {
    std::vector<Node> files;
    for (size_t i = 0; i < per_directory; ++i) {
      char name[16];
      std::snprintf(name, sizeof(name), "f%05zu", i);
      files.push_back(MakeFile(name, {}));
    }
    top.push_back(MakeDir("dir" + std::to_string(d), std::move(files)));
  }
  return top;
}

void TestLimits() {
  Section("nesting depth, path length and entry count are capped");
  Scratch scratch(g_scratch_base);
  xdvdfs::Image image;
  CHECK_IMAGE(OpenImage(BuildImage(NestedDirectories(xdvdfs::kMaxDepth)), image, scratch.path()), xdvdfs::Status::kOk);
  CHECK(image.files().size() == 1);
  CHECK_IMAGE(OpenImage(BuildImage(NestedDirectories(xdvdfs::kMaxDepth + 1)), image, scratch.path()),
              xdvdfs::Status::kCorrupt);
  CHECK_IMAGE(OpenImage(BuildImage(NestedDirectories(10, std::string(60, 'p'))), image, scratch.path()),
              xdvdfs::Status::kCorrupt);

  CHECK_IMAGE(OpenImage(BuildImage(ManyFiles(21, 12000)), image, scratch.path()), xdvdfs::Status::kOk);
  CHECK(image.files().size() == 21 * 12000);
  CHECK_IMAGE(OpenImage(BuildImage(ManyFiles(22, 12000)), image, scratch.path()), xdvdfs::Status::kCorrupt);
}

void TestUnsafeImageNames() {
  Section("an image with an unsafe entry name is rejected whole, wherever the name is");
  Scratch scratch(g_scratch_base);
  xdvdfs::Image image;
  for (const std::string& name : kUnsafeNames) {
    BuiltImage as_file = BuildImage({MakeFile("ok.txt", Pattern(10, 1)), MakeFile(name, Pattern(10, 2))});
    CHECK_IMAGE(OpenImage(as_file, image, scratch.path()), xdvdfs::Status::kUnsafeName);
    BuiltImage as_dir = BuildImage({MakeDir("good", {MakeDir(name, {MakeFile("x", Pattern(10, 2))})})});
    CHECK_IMAGE(OpenImage(as_dir, image, scratch.path()), xdvdfs::Status::kUnsafeName);
    CHECK(image.files().empty());
  }
  CHECK_IMAGE(OpenImage(BuildImage({MakeFile("A.txt", {}), MakeFile("a.TXT", {})}), image, scratch.path()),
              xdvdfs::Status::kUnsafeName);
  CHECK_IMAGE(OpenImage(BuildImage({MakeDir("X"), MakeFile("x", Pattern(5, 1))}), image, scratch.path()),
              xdvdfs::Status::kUnsafeName);
  CHECK_IMAGE(OpenImage(BuildImage({MakeDir("one", {MakeFile("a", {})}), MakeDir("two", {MakeFile("A", {})}),
                                    MakeFile("a", {})}),
                        image, scratch.path()),
              xdvdfs::Status::kOk);
  for (const Shape shape : {Shape::kBalanced, Shape::kRightChain, Shape::kLeftChain}) {
    ImageOptions options;
    options.shape = shape;
    CHECK_IMAGE(OpenImage(BuildImage({MakeFile("A.txt", {}), MakeFile("m", {}), MakeFile("a.TXT", {})}, options), image,
                          scratch.path()),
                xdvdfs::Status::kUnsafeName);
  }
}

// ---- The game check ----

void TestCheckGameFiles() {
  Section("CheckGameFiles: ready, missing, unsupported, incomplete");
  Site site;
  const GameDescriptor& game = site.descriptor;
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kMissing);
  CHECK(CheckGameFiles({}, game).state == GameFiles::kMissing);
  fs::create_directories(site.game);
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kMissing);
  WriteFile(site.parent / "afile", Text("x"));
  CHECK(CheckGameFiles(site.parent / "afile", game).state == GameFiles::kMissing);

  WriteTree(site.game, {MakeFile("DEFAULT.XEX", site.xex), MakeDir("audio", {MakeFile("A.BIN", Pattern(3000, 2))})});
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kIncomplete);
  fs::remove_all(site.game);

  WriteTree(site.game, GameTree(site.xex));
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kReady);
  fs::remove_all(site.game);

  WriteTree(site.game, LowerCased(GameTree(site.xex)));
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kReady);

  fs::remove(site.game / "big" / "big.bin");
  GameFiles missing = CheckGameFiles(site.game, game);
  CHECK(missing.state == GameFiles::kIncomplete);
  CHECK(missing.detail.find("big.bin") != std::string::npos);
  WriteFile(site.game / "big" / "big.bin", Pattern(kBigSize - 1, 5));
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kIncomplete);
  WriteFile(site.game / "big" / "big.bin", Pattern(kBigSize + 1, 5));
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kIncomplete);
  fs::remove(site.game / "big" / "big.bin");
  fs::create_directories(site.game / "big" / "big.bin");
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kIncomplete);
  fs::remove_all(site.game / "big");
  WriteFile(site.game / "big" / "big.bin", Pattern(kBigSize, 5));
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kReady);

  WriteFile(site.game / "default.xex", Pattern(kXexSize + 1, 1));
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kUnsupported);
  WriteFile(site.game / "default.xex", Pattern(kXexSize, 99));
  GameFiles other_build = CheckGameFiles(site.game, game);
  CHECK(other_build.state == GameFiles::kUnsupported);
  CHECK(other_build.detail.find("SHA-256") != std::string::npos);
  Bytes flipped = site.xex;
  flipped.back() ^= 1;
  WriteFile(site.game / "default.xex", flipped);
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kUnsupported);
  WriteFile(site.game / "default.xex", site.xex);
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kReady);

  WriteFile(site.game / "unrelated.txt", Text("extra files are fine"));
  CHECK(CheckGameFiles(site.game, game).state == GameFiles::kReady);
}

void TestCrackdownDescriptor() {
  Section("the real descriptor is well formed");
  const GameDescriptor& game = CrackdownGame();
  CHECK(game.xex_name == "default.xex");
  CHECK(game.xex_size == 14270464);
  CHECK(ToHex(game.xex_sha256) == "7AAA7A8920D1C3C737E58CF583DEBAFE6013BE0A96BEC3EC136E5DE7E3377E5A");
  CHECK(game.required_files.size() == 196);
  CHECK(TotalBytes(game) == 6555220594ull);
  CHECK(game.optional_prefixes == std::vector<std::string>{"$SystemUpdate/"});

  std::set<std::string> seen;
  bool sorted = true, safe = true, has_xex = false, optional_listed = false;
  std::string previous;
  for (const GameDescriptor::File& file : game.required_files) {
    sorted = sorted && (previous.empty() || previous < file.path);
    previous = file.path;
    safe = safe && seen.insert(Lower(file.path)).second;
    size_t start = 0;
    while (start <= file.path.size()) {
      const size_t end = std::min(file.path.find('/', start), file.path.size());
      safe = safe && xdvdfs::IsSafeName(file.path.substr(start, end - start));
      start = end + 1;
    }
    has_xex = has_xex || (Lower(file.path) == game.xex_name && file.size == game.xex_size);
    optional_listed = optional_listed || Lower(file.path).starts_with("$systemupdate/");
  }
  CHECK(sorted);
  CHECK(safe);
  CHECK(has_xex);
  CHECK(!optional_listed);
}

// ---- Installing ----

void TestInstallFromImage() {
  Section("install from a disc image: only the required files, with the image's casing");
  Site site;
  site.MakeImage();
  Recorder recorder;
  const InstallResult result = site.Install(site.image, recorder.hooks());
  CHECK_INSTALL(result, InstallStatus::kOk);
  CHECK(result.ok());
  CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
  CHECK(Listing(site.game) == kInstalledListing);
  CHECK(InstalledFromTree(site.game));
  CHECK(ReadFile(site.game / "Default.xex") == site.xex);
  CHECK(NoLeftovers(site));
  CHECK(Listing(site.parent).size() == kInstalledListing.size() + 1);

  Section("progress: phases in order, bytes never decrease, the last call is complete");
  const uint64_t total = TotalBytes(site.descriptor);
  CHECK(!recorder.calls.empty());
  std::vector<std::string> phases;
  uint64_t previous = 0;
  bool monotonic = true, constant_total = true;
  for (const InstallProgress& call : recorder.calls) {
    monotonic = monotonic && call.done_bytes >= previous && call.done_bytes <= call.total_bytes;
    constant_total = constant_total && call.total_bytes == total;
    previous = call.done_bytes;
    if (phases.empty() || phases.back() != call.phase) {
      phases.push_back(call.phase);
    }
  }
  CHECK(monotonic);
  CHECK(constant_total);
  CHECK(phases == (std::vector<std::string>{kCopying, kVerifying, kFinishing}));
  CHECK(recorder.calls.front().done_bytes == 0);
  CHECK(recorder.calls.back().done_bytes == total);
  CHECK(recorder.calls.back().total_bytes == total);
  CHECK(recorder.calls.size() == 10);
}

void TestInstallFromImageAtEveryOffset() {
  Section("install from images with the volume at each partition offset and each tree shape");
  for (const uint64_t offset : {uint64_t(0xFD90000), uint64_t(0x2090000), uint64_t(0x18300000)}) {
    for (const Shape shape : {Shape::kBalanced, Shape::kRightChain, Shape::kLeftChain}) {
      Site site;
      ImageOptions options;
      options.partition_offset = offset;
      options.shape = shape;
      site.MakeImage(options);
      CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
      CHECK(Listing(site.game) == kInstalledListing);
      CHECK(InstalledFromTree(site.game));
    }
  }
}

void TestInstallFromFolder() {
  Section("install from a folder of extracted files, matched case-insensitively");
  for (const bool lower_case : {false, true}) {
    Site site;
    site.MakeFolder(lower_case);
    Recorder recorder;
    CHECK_INSTALL(site.Install(site.folder, recorder.hooks()), InstallStatus::kOk);
    CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
    std::set<std::string> expected;
    for (const std::string& name : kInstalledListing) {
      expected.insert(lower_case ? Lower(name) : name);
    }
    CHECK(Listing(site.game) == expected);
    CHECK(!fs::exists(site.game / (lower_case ? "$systemupdate" : "$SystemUpdate")));
    CHECK(InstalledFromTree(site.game, lower_case));
    CHECK(NoLeftovers(site));
    CHECK(!recorder.calls.empty() && recorder.calls.back().done_bytes == TotalBytes(site.descriptor));
  }
}

void TestFolderSourceDetails() {
  Section("folder sources: nested install folder names, extra files, the optional prefix");
  Site site;
  site.MakeFolder();
  WriteFile(site.folder / "readme.txt", Text("not part of the game"));
  WriteFile(site.folder / "AUDIO" / "other.bin", Text("not required"));
  CHECK_INSTALL(site.Install(site.folder), InstallStatus::kOk);
  CHECK(Listing(site.game) == kInstalledListing);
}

void TestInstallIntoExistingFolders() {
  Section("the game folder may be missing, empty or an earlier install");
  {
    Site site;
    site.MakeImage();
    fs::create_directories(site.game);
    CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
    CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
    CHECK(NoLeftovers(site));
  }
  {
    Site site;
    site.MakeImage();
    CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
    WriteFile(site.game / "stale.txt", Text("leftover"));
    CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
    CHECK(!fs::exists(site.game / "stale.txt"));
    CHECK(Listing(site.game) == kInstalledListing);
    CHECK(NoLeftovers(site));
  }
}

void TestReinstallKeepsOldUntilSwap() {
  Section("reinstall: the old install stays until the swap, then it is replaced");
  Site site;
  site.MakeImage();
  MakeOldInstall(site.game);
  bool old_intact = true;
  bool staging_seen = false;
  int calls = 0;
  InstallHooks hooks;
  hooks.progress = [&](const InstallProgress& progress) {
    ++calls;
    old_intact = old_intact && OldIntact(site.game);
    if (progress.done_bytes > 0 && progress.done_bytes < progress.total_bytes) {
      staging_seen = staging_seen || fs::exists(site.staging);
    }
    return true;
  };
  CHECK_INSTALL(site.Install(site.image, hooks), InstallStatus::kOk);
  CHECK(calls > 3);
  CHECK(old_intact);
  CHECK(staging_seen);
  CHECK(!fs::exists(site.game / "marker.txt"));
  CHECK(!fs::exists(site.game / "data"));
  CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
  CHECK(Listing(site.game) == kInstalledListing);
  CHECK(NoLeftovers(site));
}

void TestUserDataNextToGameSurvives() {
  Section("files next to the game folder are never touched");
  Site site;
  site.MakeImage();
  WriteFile(site.parent / "settings.toml", Text("fullscreen = true"));
  WriteFile(site.parent / "user_data" / "save.bin", Pattern(900, 9));
  WriteFile(site.parent / "logs" / "run.log", Pattern(200, 10));
  MakeOldInstall(site.game);
  const auto others_before = [&] {
    std::set<std::string> out;
    for (const std::string& path : Listing(site.parent)) {
      if (path != "game" && path.rfind("game/", 0) != 0) {
        out.insert(path);
      }
    }
    return out;
  };
  const auto before = others_before();
  CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
  CHECK(others_before() == before);
  CHECK(ReadFile(site.parent / "user_data" / "save.bin") == Pattern(900, 9));
}

void TestWrongVersion() {
  Section("a different build of the executable is refused before anything is written");
  const Bytes other_size = Pattern(kXexSize + 5, 1);
  const Bytes same_size = Pattern(kXexSize, 99);
  Bytes flipped = TestXex();
  flipped[kXexSize / 2] ^= 0x01;
  for (const Bytes* xex : {&other_size, &same_size, static_cast<const Bytes*>(&flipped)}) {
    for (const bool from_image : {true, false}) {
      Site site;
      if (from_image) {
        WriteImage(site.image, BuildImage(GameTree(*xex)));
      } else {
        WriteTree(site.folder, GameTree(*xex));
      }
      MakeOldInstall(site.game);
      const auto before = Listing(site.parent);
      int calls = 0;
      InstallHooks hooks;
      hooks.progress = [&](const InstallProgress&) {
        ++calls;
        return true;
      };
      CHECK_INSTALL(site.Install(from_image ? site.image : site.folder, hooks), InstallStatus::kWrongVersion);
      CHECK(calls == 0);
      CHECK(OldIntact(site.game));
      CHECK(Listing(site.parent) == before);
    }
  }
}

void TestNotAnInstallSource() {
  Section("things that are not an install source");
  {
    Site site;
    MakeOldInstall(site.game);
    const auto before = Listing(site.parent);
    CHECK_INSTALL(site.Install({}), InstallStatus::kNoSource);
    CHECK_INSTALL(site.Install(site.scratch.path() / "missing.iso"), InstallStatus::kNoSource);
    CHECK_INSTALL(site.Install(site.scratch.path() / "missing" / "deeper"), InstallStatus::kNoSource);

    WriteFile(site.scratch.path() / "garbage.iso", Pattern(300000, 5));
    WriteFile(site.scratch.path() / "tiny.iso", Pattern(10, 5));
    WriteFile(site.scratch.path() / "empty.iso", {});
    CHECK_INSTALL(site.Install(site.scratch.path() / "garbage.iso"), InstallStatus::kNotAnInstallSource);
    CHECK_INSTALL(site.Install(site.scratch.path() / "tiny.iso"), InstallStatus::kNotAnInstallSource);
    CHECK_INSTALL(site.Install(site.scratch.path() / "empty.iso"), InstallStatus::kNotAnInstallSource);

    fs::create_directories(site.scratch.path() / "emptydir");
    CHECK_INSTALL(site.Install(site.scratch.path() / "emptydir"), InstallStatus::kNotAnInstallSource);
    WriteFile(site.scratch.path() / "nested" / "sub" / "default.xex", site.xex);
    CHECK_INSTALL(site.Install(site.scratch.path() / "nested"), InstallStatus::kNotAnInstallSource);
    WriteTree(site.scratch.path() / "noxex", {MakeDir("Audio", {MakeFile("a.bin", Pattern(3000, 2))})});
    CHECK_INSTALL(site.Install(site.scratch.path() / "noxex"), InstallStatus::kNotAnInstallSource);

    WriteImage(site.image, BuildImage({MakeDir("Audio", {MakeFile("a.bin", Pattern(3000, 2))})}));
    CHECK_INSTALL(site.Install(site.image), InstallStatus::kNotAnInstallSource);
    CHECK(OldIntact(site.game));
    CHECK(Listing(site.parent) == before);
  }
  {
    Site site;
    const fs::path loop = site.scratch.path() / "loop";
    const fs::path other = site.scratch.path() / "other";
    std::error_code ec;
    fs::create_symlink("loop", loop, ec);
    if (ec) {
      // Two junctions that point at each other; the second can only be made once the first one's target is gone.
      fs::create_directories(other);
      const bool made = MakeDirectoryLink(other, loop);
      fs::remove(other, ec);
      if (!made || !MakeDirectoryLink(loop, other)) {
        std::printf("  skipped the link loop: no symbolic link or junction can be created here\n");
        RemoveLink(loop);
        return;
      }
    }
    CHECK_INSTALL(site.Install(loop), InstallStatus::kNotAnInstallSource);
    CHECK(!fs::exists(site.game));
    RemoveLink(loop);
    RemoveLink(other);
  }
}

void TestIncompleteSources() {
  Section("a missing, short or lost file stops the install before copying");
  struct Case {
    const char* what;
    std::function<void(Site&)> make;
    bool from_image;
  };
  const std::vector<Case> cases = {
      {"image without a required file",
       [](Site& s) {
         WriteImage(s.image, BuildImage({MakeFile("Default.xex", s.xex), MakeDir("AUDIO", {MakeFile("a.bin", Pattern(3000, 2))})}));
       },
       true},
      {"image with a file of the wrong size",
       [](Site& s) {
         std::vector<Node> tree = GameTree(s.xex);
         tree[2].children[0].children[0].data = Pattern(5001, 4);
         WriteImage(s.image, BuildImage(tree));
       },
       true},
      {"image cut inside the big file",
       [](Site& s) {
         ImageOptions cut;
         cut.cut = 4 * kSector + 100;
         WriteImage(s.image, BuildImage(GameTree(s.xex), cut));
       },
       true},
      {"image cut inside a directory",
       [](Site& s) {
         BuiltImage built = BuildImage(GameTree(s.xex));
         PutLe32(built.bytes, built.entry_at["Local"] + 4, 0x7FFFFFFF);
         WriteImage(s.image, built);
       },
       true},
      {"image whose executable lies beyond its end",
       [](Site& s) {
         BuiltImage built = BuildImage(GameTree(s.xex));
         PutLe32(built.bytes, built.entry_at["Default.xex"] + 4, 0x7FFFFFFF);
         WriteImage(s.image, built);
       },
       true},
      {"folder without a required file",
       [](Site& s) {
         s.MakeFolder();
         fs::remove(s.folder / "Local" / "en-US" / "Strings.str");
       },
       false},
      {"folder with a short file",
       [](Site& s) {
         s.MakeFolder();
         WriteFile(s.folder / "Big" / "big.bin", Pattern(kBigSize - 1, 5));
       },
       false},
      {"folder with a long file",
       [](Site& s) {
         s.MakeFolder();
         WriteFile(s.folder / "AUDIO" / "a.bin", Pattern(3001, 2));
       },
       false},
      {"folder where a file should be a file and is a directory",
       [](Site& s) {
         s.MakeFolder();
         fs::remove(s.folder / "empty.txt");
         fs::create_directories(s.folder / "empty.txt");
       },
       false},
  };
  for (const Case& c : cases) {
    Site site;
    c.make(site);
    MakeOldInstall(site.game);
    const auto before = Listing(site.parent);
    const InstallResult result = site.Install(c.from_image ? site.image : site.folder);
    if (result.status != InstallStatus::kIncomplete) {
      std::printf("  unexpected status for: %s\n", c.what);
    }
    CHECK_INSTALL(result, InstallStatus::kIncomplete);
    CHECK(OldIntact(site.game));
    CHECK(Listing(site.parent) == before);
  }
}

void TestHostileImages() {
  Section("hostile images are refused and leave everything as it was");
  struct Case {
    const char* what;
    std::function<BuiltImage(const Bytes&)> make;
    InstallStatus expected;
  };
  const std::vector<Case> cases = {
      {"entry named ..",
       [](const Bytes& xex) {
         auto tree = GameTree(xex);
         tree.push_back(MakeFile("..", Pattern(10, 1)));
         return BuildImage(tree);
       },
       InstallStatus::kNotAnInstallSource},
      {"absolute name",
       [](const Bytes& xex) {
         auto tree = GameTree(xex);
         tree.push_back(MakeFile("C:\\evil.txt", Pattern(10, 1)));
         return BuildImage(tree);
       },
       InstallStatus::kNotAnInstallSource},
      {"slash in a name",
       [](const Bytes& xex) {
         auto tree = GameTree(xex);
         tree[2].children[0].children[0].name = "..\\..\\evil.txt";
         return BuildImage(tree);
       },
       InstallStatus::kNotAnInstallSource},
      {"device name",
       [](const Bytes& xex) {
         auto tree = GameTree(xex);
         tree[1].children.push_back(MakeFile("CON.txt", Pattern(10, 1)));
         return BuildImage(tree);
       },
       InstallStatus::kNotAnInstallSource},
      {"trailing dot",
       [](const Bytes& xex) {
         auto tree = GameTree(xex);
         tree.push_back(MakeFile("evil.", Pattern(10, 1)));
         return BuildImage(tree);
       },
       InstallStatus::kNotAnInstallSource},
      {"names that differ only in case",
       [](const Bytes& xex) {
         auto tree = GameTree(xex);
         tree[1].children.push_back(MakeFile("A.BIN", Pattern(10, 1)));
         return BuildImage(tree);
       },
       InstallStatus::kNotAnInstallSource},
      {"directory cycle",
       [](const Bytes& xex) {
         BuiltImage built = BuildImage(GameTree(xex));
         PutLe32(built.bytes, built.entry_at["AUDIO/Deep"] + 4, built.sector_of["AUDIO"]);
         return built;
       },
       InstallStatus::kNotAnInstallSource},
      {"entry tree cycle",
       [](const Bytes& xex) {
         BuiltImage built = BuildImage(GameTree(xex));
         PutLe16(built.bytes, built.entry_at["Big"] + 2, uint16_t((built.entry_at["Big"] % kSector) / 4));
         return built;
       },
       InstallStatus::kNotAnInstallSource},
      {"directory beyond the end of the image",
       [](const Bytes& xex) {
         BuiltImage built = BuildImage(GameTree(xex));
         PutLe32(built.bytes, built.entry_at["Big"] + 4, 0x40000000);
         return built;
       },
       InstallStatus::kIncomplete},
      {"directory larger than the image",
       [](const Bytes& xex) {
         BuiltImage built = BuildImage(GameTree(xex));
         PutLe32(built.bytes, built.entry_at["Big"] + 8, 0x7FFFF000);
         return built;
       },
       InstallStatus::kIncomplete},
      {"root directory beyond the end of the image",
       [](const Bytes& xex) {
         BuiltImage built = BuildImage(GameTree(xex));
         PutLe32(built.bytes, kRootFieldsAt, 0x7FFFFFF0);
         return built;
       },
       InstallStatus::kIncomplete},
      {"nested too deeply",
       [](const Bytes& xex) {
         auto tree = GameTree(xex);
         for (auto& node : NestedDirectories(xdvdfs::kMaxDepth + 1)) {
           tree.push_back(std::move(node));
         }
         return BuildImage(tree);
       },
       InstallStatus::kNotAnInstallSource},
  };
  for (const Case& c : cases) {
    Site site;
    WriteImage(site.image, c.make(site.xex));
    MakeOldInstall(site.game);
    const auto before = Listing(site.parent);
    const InstallResult result = site.Install(site.image);
    if (result.status != c.expected) {
      std::printf("  unexpected status for: %s\n", c.what);
    }
    CHECK_INSTALL(result, c.expected);
    CHECK(OldIntact(site.game));
    CHECK(Listing(site.parent) == before);
  }
}

void TestCancel() {
  Section("cancelling at any progress call leaves the old install and no work folders");
  size_t total_calls = 0;
  {
    Site site;
    site.MakeImage();
    Recorder recorder;
    CHECK_INSTALL(site.Install(site.image, recorder.hooks()), InstallStatus::kOk);
    total_calls = recorder.calls.size();
  }
  CHECK(total_calls == 10);
  for (const bool existing : {true, false}) {
    for (size_t cancel_at = 0; cancel_at < total_calls; ++cancel_at) {
      Site site;
      site.MakeImage();
      if (existing) {
        MakeOldInstall(site.game);
      }
      size_t calls = 0;
      bool staging_during_copy = true;
      InstallHooks hooks;
      hooks.progress = [&](const InstallProgress& progress) {
        if (calls > 0 && progress.phase == kCopying) {
          staging_during_copy = staging_during_copy && fs::exists(site.staging);
        }
        return calls++ != cancel_at;
      };
      const InstallResult result = site.Install(site.image, hooks);
      if (result.status != InstallStatus::kCancelled) {
        std::printf("  cancel at call %zu (existing=%d) gave status %d\n", cancel_at, int(existing), int(result.status));
      }
      CHECK_INSTALL(result, InstallStatus::kCancelled);
      CHECK(calls == cancel_at + 1);
      CHECK(staging_during_copy);
      CHECK(NoLeftovers(site));
      if (existing) {
        CHECK(OldIntact(site.game));
        CHECK(Listing(site.game).size() == 4);
      } else {
        CHECK(!fs::exists(site.game));
      }
    }
  }
}

void TestCancelFromFolder() {
  Section("cancelling a folder install mid-copy");
  Site site;
  site.MakeFolder();
  MakeOldInstall(site.game);
  InstallHooks hooks;
  hooks.progress = [](const InstallProgress& progress) { return progress.done_bytes < 3000; };
  CHECK_INSTALL(site.Install(site.folder, hooks), InstallStatus::kCancelled);
  CHECK(OldIntact(site.game));
  CHECK(NoLeftovers(site));
}

void TestBadCopyIsNotActivated() {
  Section("a copy that does not verify is never swapped in");
  struct Case {
    const char* what;
    std::function<void(const fs::path& staging)> damage;
    InstallStatus expected;
  };
  const std::vector<Case> cases = {
      {"another build of the executable",
       [](const fs::path& staging) { WriteFile(staging / "Default.xex", Pattern(kXexSize, 99)); },
       InstallStatus::kWrongVersion},
      {"a file of the wrong size",
       [](const fs::path& staging) { WriteFile(staging / "Big" / "big.bin", Pattern(10, 1)); },
       InstallStatus::kIncomplete},
      {"a missing file", [](const fs::path& staging) { fs::remove(staging / "AUDIO" / "a.bin"); },
       InstallStatus::kIncomplete},
  };
  for (const Case& c : cases) {
    Site site;
    site.MakeImage();
    MakeOldInstall(site.game);
    InstallHooks hooks;
    hooks.progress = [&](const InstallProgress& progress) {
      if (progress.phase == kVerifying) {
        c.damage(site.staging);
      }
      return true;
    };
    const InstallResult result = site.Install(site.image, hooks);
    if (result.status != c.expected) {
      std::printf("  unexpected status for: %s\n", c.what);
    }
    CHECK_INSTALL(result, c.expected);
    CHECK(OldIntact(site.game));
    CHECK(NoLeftovers(site));
  }
}

void TestThrowingHook() {
  Section("an exception from the progress hook ends the install cleanly");
  Site site;
  site.MakeImage();
  MakeOldInstall(site.game);
  int calls = 0;
  InstallHooks hooks;
  hooks.progress = [&](const InstallProgress&) -> bool {
    if (++calls == 4) {
      throw std::runtime_error("hook failed");
    }
    return true;
  };
  const InstallResult result = site.Install(site.image, hooks);
  CHECK_INSTALL(result, InstallStatus::kIoError);
  CHECK(result.detail.find("hook failed") != std::string::npos);
  CHECK(OldIntact(site.game));
  CHECK(NoLeftovers(site));
}

void TestForeignFolder() {
  Section("a folder of unrelated files is never replaced");
  Site site;
  site.MakeImage();
  WriteFile(site.game / "photos" / "a.jpg", Pattern(100, 1));
  WriteFile(site.game / "notes.txt", Text("mine"));
  const auto before = Listing(site.parent);
  CHECK_INSTALL(site.Install(site.image), InstallStatus::kForeignFolder);
  CHECK(ReadFile(site.game / "photos" / "a.jpg") == Pattern(100, 1));
  CHECK(Listing(site.parent) == before);

  fs::remove_all(site.game);
  WriteFile(site.game, Text("a file where the folder should be"));
  CHECK_INSTALL(site.Install(site.image), InstallStatus::kNoGameFolder);
  CHECK(ReadFile(site.game) == Text("a file where the folder should be"));
}

void TestNoGameFolder() {
  Section("an unusable game folder path");
  Site site;
  site.MakeImage();
  CHECK_INSTALL(InstallGame(site.image, {}, {}, site.descriptor), InstallStatus::kNoGameFolder);
  CHECK_INSTALL(InstallGame(site.image, site.scratch.path().root_path(), {}, site.descriptor),
                InstallStatus::kNoGameFolder);
  CHECK_INSTALL(InstallGame(site.image, site.scratch.path() / "nope" / "game", {}, site.descriptor),
                InstallStatus::kNoGameFolder);
  WriteFile(site.scratch.path() / "plain.txt", Text("x"));
  CHECK_INSTALL(InstallGame(site.image, site.scratch.path() / "plain.txt" / "game", {}, site.descriptor),
                InstallStatus::kNoGameFolder);
  CHECK(!fs::exists(site.scratch.path() / "nope"));
  CHECK(!fs::exists(site.game));
}

void TestTrailingSeparatorOnGameFolder() {
  Section("a game folder written with a trailing separator or dot segments");
  Site site;
  site.MakeImage();
  const fs::path odd = site.parent / "other" / ".." / "game" / "";
  CHECK_INSTALL(InstallGame(site.image, odd, {}, site.descriptor), InstallStatus::kOk);
  CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
  CHECK(NoLeftovers(site));
}

void TestNoSpace() {
  Section("free space: the copy plus a 64 MiB margin on the volume that holds the staging folder");
  Site site;
  site.MakeImage();
  MakeOldInstall(site.game);
  const uint64_t needed = TotalBytes(site.descriptor) + (64ull << 20);
  fs::path asked;
  FileOps ops;
  ops.available_space = [&](const fs::path& path, std::error_code&) {
    asked = path;
    return uintmax_t(needed - 1);
  };
  CHECK_INSTALL(site.Install(site.image, {}, ops), InstallStatus::kNoSpace);
  CHECK(asked == site.parent);
  CHECK(OldIntact(site.game));
  CHECK(NoLeftovers(site));
  ops.available_space = [&](const fs::path&, std::error_code&) { return uintmax_t(0); };
  CHECK_INSTALL(site.Install(site.image, {}, ops), InstallStatus::kNoSpace);
  CHECK(OldIntact(site.game));
  ops.available_space = [&](const fs::path&, std::error_code&) { return uintmax_t(needed); };
  CHECK_INSTALL(site.Install(site.image, {}, ops), InstallStatus::kOk);
  CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
}

void TestSwapFailures() {
  Section("a failed swap restores the old install");
  {
    Site site;
    site.MakeImage();
    MakeOldInstall(site.game);
    int attempts = 0;
    const FileOps ops = FailingRenames(IsTargetToBackup, &attempts);
    CHECK_INSTALL(site.Install(site.image, {}, ops), InstallStatus::kIoError);
    CHECK(attempts == 10);
    CHECK(OldIntact(site.game));
    CHECK(NoLeftovers(site));
  }
  {
    Site site;
    site.MakeImage();
    MakeOldInstall(site.game);
    int attempts = 0;
    const FileOps ops = FailingRenames(
        [&](const fs::path& from, const fs::path& to) {
          if (IsStagingToTarget(from, to)) {
            ++attempts;
            return true;
          }
          return false;
        });
    CHECK_INSTALL(site.Install(site.image, {}, ops), InstallStatus::kIoError);
    CHECK(attempts == 10);
    CHECK(OldIntact(site.game));
    CHECK(NoLeftovers(site));
  }
  {
    Site site;
    site.MakeImage();
    int attempts = 0;
    const FileOps ops = FailingRenames(IsStagingToTarget, &attempts);
    CHECK_INSTALL(site.Install(site.image, {}, ops), InstallStatus::kIoError);
    CHECK(attempts == 10);
    CHECK(!fs::exists(site.game));
    CHECK(NoLeftovers(site));
  }
  {
    Site site;
    site.MakeImage();
    MakeOldInstall(site.game);
    const FileOps ops = FailingRenames(
        [](const fs::path& from, const fs::path& to) { return IsStagingToTarget(from, to) || IsBackupToTarget(from, to); });
    const InstallResult result = site.Install(site.image, {}, ops);
    CHECK_INSTALL(result, InstallStatus::kIoError);
    CHECK(result.detail.find("previous install") != std::string::npos);
    CHECK(!fs::exists(site.game));
    CHECK(!fs::exists(site.staging));
    CHECK(OldIntact(site.backup));
    CHECK(RecoverInterruptedInstall(site.game, site.descriptor));
    CHECK(OldIntact(site.game));
    CHECK(NoLeftovers(site));
    CHECK(!RecoverInterruptedInstall(site.game, site.descriptor));
  }
}

void TestRenameRetry() {
  Section("renames are retried, 10 attempts, and succeed after a transient failure");
  const FileOps defaults{};
  CHECK(defaults.rename_attempts == 10);
  CHECK(defaults.retry_delay == milliseconds(300));

  Site site;
  site.MakeImage();
  MakeOldInstall(site.game);
  int failures_left = 4;
  int attempts = 0;
  const FileOps ops = FailingRenames(
      [&](const fs::path& from, const fs::path& to) {
        if (IsStagingToTarget(from, to) && failures_left > 0) {
          --failures_left;
          ++attempts;
          return true;
        }
        return false;
      });
  CHECK_INSTALL(site.Install(site.image, {}, ops), InstallStatus::kOk);
  CHECK(attempts == 4);
  CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
  CHECK(NoLeftovers(site));

  Site slow;
  slow.MakeImage();
  FileOps delayed = FailingRenames(IsStagingToTarget);
  delayed.retry_delay = milliseconds(20);
  delayed.rename_attempts = 4;
  const auto started = std::chrono::steady_clock::now();
  CHECK_INSTALL(slow.Install(slow.image, {}, delayed), InstallStatus::kIoError);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  CHECK(elapsed >= milliseconds(60));
}

void TestBackupRemovalFailure() {
  Section("a backup that cannot be removed after the swap does not fail the install");
  Site site;
  site.MakeImage();
  MakeOldInstall(site.game);
  FileOps ops;
  ops.remove_all = [](const fs::path&, std::error_code& ec) { ec = std::make_error_code(std::errc::permission_denied); };
  const InstallResult result = site.Install(site.image, {}, ops);
  CHECK_INSTALL(result, InstallStatus::kOk);
  CHECK(!result.detail.empty());
  CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
  CHECK(OldIntact(site.backup));
  CHECK(!RecoverInterruptedInstall(site.game, site.descriptor));
  CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
  CHECK(NoLeftovers(site));
}

void TestRecoverInterruptedInstall() {
  Section("RecoverInterruptedInstall puts a lost install back");
  {
    Site site;
    CHECK(!RecoverInterruptedInstall(site.game, site.descriptor));
    CHECK(!RecoverInterruptedInstall({}, site.descriptor));
    CHECK(!RecoverInterruptedInstall(site.scratch.path() / "nope" / "game", site.descriptor));
  }
  {
    Site site;
    MakeOldInstall(site.backup);
    CHECK(RecoverInterruptedInstall(site.game, site.descriptor));
    CHECK(OldIntact(site.game));
    CHECK(!fs::exists(site.backup));
    CHECK(!RecoverInterruptedInstall(site.game, site.descriptor));
  }
  {
    Site site;
    WriteFile(site.backup / "DEFAULT.XEX", Bytes{1});
    WriteFile(site.staging / "partial.bin", Bytes{1});
    CHECK(RecoverInterruptedInstall(site.game));
    CHECK(fs::exists(site.game / "DEFAULT.XEX"));
    CHECK(fs::exists(site.staging));
  }
  {
    Site site;
    MakeOldInstall(site.game);
    WriteFile(site.backup / "default.xex", Bytes{7});
    CHECK(!RecoverInterruptedInstall(site.game, site.descriptor));
    CHECK(OldIntact(site.game));
    CHECK(fs::exists(site.backup / "default.xex"));
  }
  {
    Site site;
    WriteFile(site.backup / "something.txt", Bytes{7});
    CHECK(!RecoverInterruptedInstall(site.game, site.descriptor));
    CHECK(fs::exists(site.backup / "something.txt"));
    CHECK(!fs::exists(site.game));
  }
  {
    Site site;
    MakeOldInstall(site.backup);
    const FileOps ops = FailingRenames(IsBackupToTarget);
    CHECK(!RecoverInterruptedInstall(site.game, site.descriptor, ops));
    CHECK(OldIntact(site.backup));
  }
  {
    Site site;
    MakeOldInstall(site.scratch.path() / "elsewhere");
    if (MakeDirectoryLink(site.scratch.path() / "elsewhere", site.backup)) {
      CHECK(!RecoverInterruptedInstall(site.game, site.descriptor));
      CHECK(!fs::exists(site.game));
      RemoveLink(site.backup);
      CHECK(OldIntact(site.scratch.path() / "elsewhere"));
    } else {
      std::printf("  skipped the linked backup: no symbolic link or junction can be created here\n");
    }
  }
}

void TestStaleWorkFolders() {
  Section("work folders left by an interrupted install");
  {
    Site site;
    site.MakeImage();
    WriteFile(site.staging / "Audio" / "half.bin", Pattern(500, 1));
    WriteFile(site.staging / "junk.txt", Text("x"));
    MakeOldInstall(site.game);
    CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
    CHECK(Listing(site.game) == kInstalledListing);
    CHECK(NoLeftovers(site));
  }
  {
    Site site;
    site.MakeImage();
    MakeOldInstall(site.backup);
    MakeOldInstall(site.game);
    WriteFile(site.game / "default.xex", Bytes{4, 4});
    CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
    CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
    CHECK(NoLeftovers(site));
  }
  {
    Site site;
    site.MakeImage();
    MakeOldInstall(site.backup);
    WriteFile(site.staging / "complete.txt", Text("a finished staging folder from a crash"));
    CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
    CHECK(CheckGameFiles(site.game, site.descriptor).state == GameFiles::kReady);
    CHECK(NoLeftovers(site));
  }
  {
    Site site;
    WriteImage(site.image, BuildImage(GameTree(Pattern(kXexSize, 99))));
    MakeOldInstall(site.backup);
    CHECK_INSTALL(site.Install(site.image), InstallStatus::kWrongVersion);
    CHECK(OldIntact(site.backup));
    CHECK(RecoverInterruptedInstall(site.game, site.descriptor));
    CHECK(OldIntact(site.game));
  }
  {
    Site site;
    site.MakeImage();
    MakeOldInstall(site.backup);
    const FileOps ops = FailingRenames(IsBackupToTarget);
    CHECK_INSTALL(site.Install(site.image, {}, ops), InstallStatus::kIoError);
    CHECK(OldIntact(site.backup));
    CHECK(!fs::exists(site.game));
  }
}

void TestOverlappingFolders() {
  Section("the source and the game folder must not be, or contain, one another");
  {
    Site site;
    site.MakeFolder();
    CHECK_INSTALL(InstallGame(site.folder, site.folder, {}, site.descriptor), InstallStatus::kUnsafeTarget);
    CHECK_INSTALL(InstallGame(site.folder, site.folder / "game", {}, site.descriptor), InstallStatus::kUnsafeTarget);
    CHECK_INSTALL(InstallGame(site.folder, site.folder / "Audio", {}, site.descriptor), InstallStatus::kUnsafeTarget);
    CHECK_INSTALL(InstallGame(site.folder, site.scratch.path(), {}, site.descriptor), InstallStatus::kUnsafeTarget);
    CHECK_INSTALL(InstallGame(site.folder / "AUDIO", site.folder, {}, site.descriptor), InstallStatus::kUnsafeTarget);
      CHECK(!fs::exists(site.folder / "game"));
  }
#if defined(_WIN32)
  {
    Site site;
    site.MakeFolder();
    const fs::path shouting = site.folder.parent_path() / "DISC";
    CHECK_INSTALL(InstallGame(shouting, site.folder / "game", {}, site.descriptor), InstallStatus::kUnsafeTarget);
  }
#endif
  {
    Site site;
    MakeOldInstall(site.game);
    const BuiltImage built = BuildImage(GameTree(site.xex));
    WriteImage(site.game / "disc.iso", built);
    CHECK_INSTALL(site.Install(site.game / "disc.iso"), InstallStatus::kUnsafeTarget);
    CHECK(fs::exists(site.game / "disc.iso"));
    CHECK(ReadFile(site.game / "marker.txt") == Text("old"));

    WriteImage(site.staging / "disc.iso", built);
    CHECK_INSTALL(site.Install(site.staging / "disc.iso"), InstallStatus::kUnsafeTarget);
    CHECK(fs::exists(site.staging / "disc.iso"));
    WriteImage(site.backup / "disc.iso", built);
    CHECK_INSTALL(site.Install(site.backup / "disc.iso"), InstallStatus::kUnsafeTarget);
    CHECK(fs::exists(site.backup / "disc.iso"));
  }
  {
    Site site;
    site.MakeImage();
    WriteFile(site.parent / "game-install-temp-notes.iso", Text("sibling names are fine"));
    CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
    CHECK(fs::exists(site.parent / "game-install-temp-notes.iso"));
  }
}

void TestLinksAreNeverFollowed() {
  Section("links in the target or the work folders stop the install before anything is deleted");
  const auto setup = [](Site& site, fs::path& decoy) {
    site.MakeImage();
    decoy = site.scratch.path() / "decoy";
    WriteFile(decoy / "keep.bin", Pattern(40, 4));
    WriteFile(decoy / "default.xex", Bytes{1, 2, 3});
  };
  bool linked_any = false;
  {
    Site site;
    fs::path decoy;
    setup(site, decoy);
    if (MakeDirectoryLink(decoy, site.staging)) {
      linked_any = true;
      MakeOldInstall(site.game);
      CHECK_INSTALL(site.Install(site.image), InstallStatus::kUnsafeTarget);
      CHECK(ReadFile(decoy / "keep.bin") == Pattern(40, 4));
      CHECK(OldIntact(site.game));
      RemoveLink(site.staging);
      CHECK(ReadFile(decoy / "keep.bin") == Pattern(40, 4));
    }
  }
  {
    Site site;
    fs::path decoy;
    setup(site, decoy);
    if (MakeDirectoryLink(decoy, site.backup)) {
      MakeOldInstall(site.game);
      CHECK_INSTALL(site.Install(site.image), InstallStatus::kUnsafeTarget);
      CHECK(ReadFile(decoy / "keep.bin") == Pattern(40, 4));
      CHECK(OldIntact(site.game));
      RemoveLink(site.backup);
    }
  }
  {
    Site site;
    fs::path decoy;
    setup(site, decoy);
    if (MakeDirectoryLink(decoy, site.backup)) {
      CHECK_INSTALL(site.Install(site.image), InstallStatus::kUnsafeTarget);
      CHECK(!RecoverInterruptedInstall(site.game, site.descriptor));
      CHECK(ReadFile(decoy / "keep.bin") == Pattern(40, 4));
      CHECK(!fs::exists(site.game));
      RemoveLink(site.backup);
    }
  }
  {
    Site site;
    fs::path decoy;
    setup(site, decoy);
    fs::create_directories(site.staging);
    WriteFile(site.staging / "real.bin", Pattern(5, 5));
    if (MakeDirectoryLink(decoy, site.staging / "inner")) {
      CHECK_INSTALL(site.Install(site.image), InstallStatus::kUnsafeTarget);
      CHECK(ReadFile(decoy / "keep.bin") == Pattern(40, 4));
      CHECK(ReadFile(site.staging / "real.bin") == Pattern(5, 5));
      RemoveLink(site.staging / "inner");
    }
  }
  {
    Site site;
    fs::path decoy;
    setup(site, decoy);
    if (MakeDirectoryLink(decoy, site.game)) {
      CHECK_INSTALL(site.Install(site.image), InstallStatus::kUnsafeTarget);
      CHECK(ReadFile(decoy / "keep.bin") == Pattern(40, 4));
      CHECK(ReadFile(decoy / "default.xex") == Bytes({1, 2, 3}));
      CHECK(NoLeftovers(site));
      RemoveLink(site.game);
      CHECK(ReadFile(decoy / "keep.bin") == Pattern(40, 4));
    }
  }
  {
    Site site;
    fs::path decoy;
    setup(site, decoy);
    MakeOldInstall(site.game);
    if (MakeDirectoryLink(decoy, site.game / "linked")) {
      CHECK_INSTALL(site.Install(site.image), InstallStatus::kUnsafeTarget);
      CHECK(ReadFile(decoy / "keep.bin") == Pattern(40, 4));
      CHECK(ReadFile(site.game / "marker.txt") == Text("old"));
      CHECK(NoLeftovers(site));
      RemoveLink(site.game / "linked");
      CHECK_INSTALL(site.Install(site.image), InstallStatus::kOk);
    }
  }
  {
    Site site;
    fs::path decoy;
    setup(site, decoy);
    std::error_code ec;
    fs::create_directories(site.staging);
    fs::create_symlink(decoy / "keep.bin", site.staging / "note.bin", ec);
    if (!ec) {
      CHECK_INSTALL(site.Install(site.image), InstallStatus::kUnsafeTarget);
      CHECK(ReadFile(decoy / "keep.bin") == Pattern(40, 4));
    } else {
      std::printf("  skipped the file symlink: none can be created here\n");
    }
  }
  if (!linked_any) {
    std::printf("  skipped the link checks: no symbolic link or junction can be created here\n");
  }
}

void TestWorkFolderThatIsAFile() {
  Section("a plain file in the place of a work folder is left alone");
  Site site;
  site.MakeImage();
  WriteFile(site.staging, Text("not a folder"));
  CHECK_INSTALL(site.Install(site.image), InstallStatus::kUnsafeTarget);
  CHECK(ReadFile(site.staging) == Text("not a folder"));
  CHECK(!fs::exists(site.game));
}

void TestOnWorkerThread() {
  Section("the install runs, reports and cancels on a worker thread");
  {
    Site site;
    site.MakeImage();
    std::atomic<uint64_t> last_done{0};
    std::thread::id worker_id;
    InstallResult result;
    InstallHooks hooks;
    hooks.progress = [&](const InstallProgress& progress) {
      worker_id = std::this_thread::get_id();
      last_done = progress.done_bytes;
      return true;
    };
    std::thread worker([&] { result = site.Install(site.image, hooks); });
    worker.join();
    CHECK(result.ok());
    CHECK(worker_id != std::this_thread::get_id());
    CHECK(last_done == TotalBytes(site.descriptor));
  }
  {
    Site site;
    site.MakeImage();
    std::atomic<bool> cancel{false};
    std::atomic<bool> in_copy{false};
    InstallHooks hooks;
    hooks.progress = [&](const InstallProgress& progress) {
      if (progress.done_bytes > 0) {
        in_copy = true;
        while (!cancel && progress.phase == kCopying) {
          std::this_thread::sleep_for(milliseconds(1));
        }
      }
      return !cancel;
    };
    InstallResult result;
    std::thread worker([&] { result = site.Install(site.image, hooks); });
    while (!in_copy) {
      std::this_thread::sleep_for(milliseconds(1));
    }
    CHECK(fs::exists(site.staging));
    cancel = true;
    worker.join();
    CHECK_INSTALL(result, InstallStatus::kCancelled);
    CHECK(!fs::exists(site.game));
    CHECK(NoLeftovers(site));
  }
}

void TestConcurrentInstallsIntoSeparateFolders() {
  Section("two installs run side by side without sharing state");
  Site a;
  Site b;
  a.MakeImage();
  b.MakeFolder();
  InstallResult ra, rb;
  std::thread ta([&] { ra = a.Install(a.image); });
  std::thread tb([&] { rb = b.Install(b.folder); });
  ta.join();
  tb.join();
  CHECK(ra.ok());
  CHECK(rb.ok());
  CHECK(CheckGameFiles(a.game, a.descriptor).state == GameFiles::kReady);
  CHECK(CheckGameFiles(b.game, b.descriptor).state == GameFiles::kReady);
}

void TestDescribeInstall() {
  Section("every status has a player-facing message");
  std::set<std::string> titles;
  for (int status = int(InstallStatus::kOk); status <= int(InstallStatus::kIoError); ++status) {
    const InstallMessage message = DescribeInstall(InstallStatus(status));
    CHECK(!message.title.empty());
    CHECK(message.text.size() > 10);
    CHECK(titles.insert(message.title).second);
  }
  CHECK(DescribeInstall(InstallStatus::kOk).title == "Game installed");
}

// ---- The real game ----

struct RealOptions {
  fs::path image;
  fs::path folder;
  fs::path reference;
  fs::path workdir;
};

void ParallelFor(size_t count, const std::function<void(size_t)>& work) {
  std::atomic<size_t> next{0};
  std::vector<std::thread> threads;
  const unsigned workers = std::clamp(std::thread::hardware_concurrency(), 2u, 6u);
  for (unsigned i = 0; i < workers; ++i) {
    threads.emplace_back([&] {
      for (size_t index = next++; index < count; index = next++) {
        work(index);
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
}

bool InstallRealAndCompare(const char* label, const fs::path& source, const RealOptions& options) {
  using Clock = std::chrono::steady_clock;
  const GameDescriptor& game = CrackdownGame();
  const fs::path work = options.workdir / label;
  const fs::path installed = work / "game";
  std::error_code ec;
  fs::remove_all(work, ec);
  fs::create_directories(work);
  bool ok = true;

  std::printf("%s install from %s\n", label, source.string().c_str());
  uint64_t next_report = 0;
  InstallHooks hooks;
  hooks.progress = [&](const InstallProgress& progress) {
    if (progress.done_bytes >= next_report) {
      std::printf("  %s: %llu of %llu MiB\n", progress.phase.c_str(),
                  static_cast<unsigned long long>(progress.done_bytes >> 20),
                  static_cast<unsigned long long>(progress.total_bytes >> 20));
      next_report = progress.done_bytes + (1000ull << 20);
    }
    return true;
  };
  const auto started = Clock::now();
  const InstallResult result = InstallGame(source, installed, hooks);
  const double install_seconds = std::chrono::duration<double>(Clock::now() - started).count();
  std::printf("  InstallGame: status %d in %.1f s %s\n", int(result.status), install_seconds, result.detail.c_str());
  ok = ok && result.ok();

  const auto check_started = Clock::now();
  const GameFiles check = CheckGameFiles(installed);
  std::printf("  CheckGameFiles: %s in %.2f s\n", check.state == GameFiles::kReady ? "ready" : check.detail.c_str(),
              std::chrono::duration<double>(Clock::now() - check_started).count());
  ok = ok && check.state == GameFiles::kReady;
  ok = ok && !fs::exists(installed.parent_path() / "game-install-temp") &&
       !fs::exists(installed.parent_path() / "game-install-backup");

  std::map<std::string, fs::path> reference_files;
  for (fs::recursive_directory_iterator it(options.reference, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file()) {
      reference_files[it->path().lexically_relative(options.reference).generic_string()] = it->path();
    }
  }
  std::vector<std::pair<fs::path, fs::path>> pairs;
  size_t optional_skipped = 0;
  for (const auto& [relative, path] : reference_files) {
    if (Lower(relative).starts_with("$systemupdate/")) {
      ++optional_skipped;
      ok = ok && !fs::exists(installed / relative);
      continue;
    }
    pairs.emplace_back(path, installed / relative);
  }
  size_t installed_count = 0;
  for (fs::recursive_directory_iterator it(installed, ec), end; !ec && it != end; it.increment(ec)) {
    installed_count += it->is_regular_file() ? 1 : 0;
  }
  std::printf("  reference: %zu files (%zu optional skipped), installed: %zu files, required: %zu\n",
              reference_files.size(), optional_skipped, installed_count, game.required_files.size());
  ok = ok && pairs.size() == game.required_files.size() && installed_count == pairs.size();

  std::atomic<size_t> mismatches{0};
  const auto compare_started = Clock::now();
  ParallelFor(pairs.size(), [&](size_t i) {
    std::error_code error;
    if (!fs::is_regular_file(pairs[i].second, error) ||
        fs::file_size(pairs[i].second, error) != fs::file_size(pairs[i].first, error) ||
        HashFile(pairs[i].second) != HashFile(pairs[i].first)) {
      std::printf("  DIFFERENT: %s\n", pairs[i].second.string().c_str());
      ++mismatches;
    }
  });
  std::printf("  compared %zu files (size and SHA-256) with the reference in %.1f s: %zu differ\n", pairs.size(),
              std::chrono::duration<double>(Clock::now() - compare_started).count(), mismatches.load());
  ok = ok && mismatches == 0;

  fs::remove_all(work, ec);
  std::printf("  %s: %s, install time %.1f s\n", label, ok ? "PASS" : "FAIL", install_seconds);
  return ok;
}

int RunRealGame(const RealOptions& options) {
  if (options.reference.empty() || !fs::is_directory(options.reference)) {
    std::printf("--reference must name the extracted game folder\n");
    return 2;
  }
  std::error_code ec;
  fs::create_directories(options.workdir, ec);
  const uint64_t needed = TotalBytes(CrackdownGame()) + (1ull << 30);
  const uint64_t available = fs::space(options.workdir, ec).available;
  std::printf("work folder %s: %llu MiB free, %llu MiB needed\n", options.workdir.string().c_str(),
              static_cast<unsigned long long>(available >> 20), static_cast<unsigned long long>(needed >> 20));
  if (ec || available < needed) {
    std::printf("not enough free space\n");
    return 2;
  }
  bool ok = true;
  if (!options.image.empty()) {
    ok = InstallRealAndCompare("image", options.image, options) && ok;
  }
  if (!options.folder.empty()) {
    ok = InstallRealAndCompare("folder", options.folder, options) && ok;
  }
  fs::remove(options.workdir, ec);
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  RealOptions real;
  g_scratch_base = fs::temp_directory_path() / ("crackdown_installer_tests_" +
                                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string flag = argv[i];
    if (flag == "--image") {
      real.image = argv[i + 1];
    } else if (flag == "--folder") {
      real.folder = argv[i + 1];
    } else if (flag == "--reference") {
      real.reference = argv[i + 1];
    } else if (flag == "--workdir") {
      real.workdir = argv[i + 1];
    } else {
      std::printf("unknown option %s\n", flag.c_str());
      return 2;
    }
  }
  if (!real.image.empty() || !real.folder.empty()) {
    if (real.workdir.empty()) {
      real.workdir = fs::temp_directory_path() / "crackdown_installer_real";
    }
    return RunRealGame(real);
  }

  fs::create_directories(g_scratch_base);
  TestSafeNames();
  TestPartitionOffsets();
  TestTreeShapes();
  TestEmptyAndLookup();
  TestNotAnImage();
  TestTruncatedImages();
  TestMalformedEntries();
  TestDirectoryLoops();
  TestLimits();
  TestUnsafeImageNames();
  TestCheckGameFiles();
  TestCrackdownDescriptor();
  TestInstallFromImage();
  TestInstallFromImageAtEveryOffset();
  TestInstallFromFolder();
  TestFolderSourceDetails();
  TestInstallIntoExistingFolders();
  TestReinstallKeepsOldUntilSwap();
  TestUserDataNextToGameSurvives();
  TestWrongVersion();
  TestNotAnInstallSource();
  TestIncompleteSources();
  TestHostileImages();
  TestCancel();
  TestCancelFromFolder();
  TestBadCopyIsNotActivated();
  TestThrowingHook();
  TestForeignFolder();
  TestNoGameFolder();
  TestTrailingSeparatorOnGameFolder();
  TestNoSpace();
  TestSwapFailures();
  TestRenameRetry();
  TestBackupRemovalFailure();
  TestRecoverInterruptedInstall();
  TestStaleWorkFolders();
  TestOverlappingFolders();
  TestLinksAreNeverFollowed();
  TestWorkFolderThatIsAFile();
  TestOnWorkerThread();
  TestConcurrentInstallsIntoSeparateFolders();
  TestDescribeInstall();

  std::error_code ec;
  fs::remove_all(g_scratch_base, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
