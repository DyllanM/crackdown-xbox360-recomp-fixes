#include "xdvdfs.h"

#include <cstring>
#include <system_error>
#include <unordered_set>

namespace crackdown::xdvdfs {
namespace {

constexpr char kVolumeMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kVolumeMagicBytes = sizeof(kVolumeMagic) - 1;
constexpr uint64_t kDescriptorSector = 32;
constexpr size_t kRootFieldsOffset = 20;
constexpr size_t kTrailingMagicOffset = 0x7EC;
// The bare volume of an extracted image, then where the game partition starts on XGD2, XGD3 and XGD1 discs.
constexpr uint64_t kPartitionOffsets[] = {0, 0xFD90000, 0x2090000, 0x18300000};

constexpr size_t kEntryHeaderBytes = 14;
constexpr uint8_t kDirectoryAttribute = 0x10;
// A child offset counts 4-byte units from the start of the directory. Zero and the 0xFF padding mean "no child".
constexpr uint16_t kPaddingChild = 0xFFFF;

uint16_t Le16(const uint8_t* p) {
  return uint16_t(p[0] | p[1] << 8);
}

uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

std::string LowerAscii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
  }
  return out;
}

std::string Printable(std::string_view text) {
  std::string out(text.substr(0, 64));
  for (char& c : out) {
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) {
      c = '?';
    }
  }
  return out;
}

bool IsReservedDeviceName(std::string_view name) {
  name = name.substr(0, name.find('.'));
  while (!name.empty() && name.back() == ' ') {
    name.remove_suffix(1);
  }
  const std::string upper = [&] {
    std::string out(name);
    for (char& c : out) {
      if (c >= 'a' && c <= 'z') {
        c = char(c - 'a' + 'A');
      }
    }
    return out;
  }();
  for (const char* reserved : {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}) {
    if (upper == reserved) {
      return true;
    }
  }
  return upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) && upper[3] >= '1' &&
         upper[3] <= '9';
}

struct Volume {
  uint64_t partition_offset = 0;
  uint32_t root_sector = 0;
  uint32_t root_size = 0;
};

// The first partition offset whose volume descriptor matches at both ends and whose root directory lies in the file.
Result LocateVolume(Image& image, Volume& volume) {
  bool cut_short = false;
  uint8_t descriptor[kSectorBytes];
  for (const uint64_t offset : kPartitionOffsets) {
    const uint64_t at = offset + kDescriptorSector * kSectorBytes;
    if (at > image.image_size() || kSectorBytes > image.image_size() - at || !image.ReadAt(at, descriptor, kSectorBytes)) {
      continue;
    }
    if (std::memcmp(descriptor, kVolumeMagic, kVolumeMagicBytes) != 0 ||
        std::memcmp(descriptor + kTrailingMagicOffset, kVolumeMagic, kVolumeMagicBytes) != 0) {
      continue;
    }
    const uint32_t sector = Le32(descriptor + kRootFieldsOffset);
    const uint32_t size = Le32(descriptor + kRootFieldsOffset + 4);
    const uint64_t root_at = offset + uint64_t(sector) * kSectorBytes;
    if (root_at > image.image_size() || size > image.image_size() - root_at) {
      cut_short = true;
      continue;
    }
    volume = {offset, sector, size};
    return {};
  }
  if (cut_short) {
    return {Status::kTruncated, "the root directory lies beyond the end of the image"};
  }
  return {Status::kNotAnImage, "no volume descriptor"};
}

// Reads every directory. The entries of one directory form a binary tree that is walked without recursion; only
// nested directories recurse, and their depth is capped.
class TreeParser {
 public:
  TreeParser(Image& image, uint64_t partition_offset, std::vector<File>& files)
      : image_(image), partition_offset_(partition_offset), files_(files) {}

  Result Parse(uint32_t root_sector, uint32_t root_size) { return Directory(root_sector, root_size, {}, 0); }

 private:
  Result Directory(uint32_t sector, uint32_t size, const std::string& prefix, size_t depth);
  Result Entry(const std::vector<uint8_t>& data, uint32_t node, const std::string& prefix, size_t depth,
               std::unordered_set<std::string>& names);

  Image& image_;
  uint64_t partition_offset_;
  std::vector<File>& files_;
  std::unordered_set<uint32_t> directories_;
  uint64_t directory_bytes_ = 0;
  size_t entries_ = 0;
};

Result TreeParser::Directory(uint32_t sector, uint32_t size, const std::string& prefix, size_t depth) {
  if (size == 0) {
    return {};
  }
  const uint64_t at = partition_offset_ + uint64_t(sector) * kSectorBytes;
  if (at > image_.image_size() || size > image_.image_size() - at) {
    return {Status::kTruncated, "directory '" + prefix + "' lies beyond the end of the image"};
  }
  if (size > kMaxDirectoryBytes - directory_bytes_) {
    return {Status::kCorrupt, "the directories are too large"};
  }
  if (!directories_.insert(sector).second) {
    return {Status::kCorrupt, "directory '" + prefix + "' was already read"};
  }
  directory_bytes_ += size;

  std::vector<uint8_t> data(size);
  if (!image_.ReadAt(at, data.data(), size)) {
    return {Status::kIoError, "cannot read directory '" + prefix + "'"};
  }
  if (size >= 4 && Le32(data.data()) == 0xFFFFFFFFu) {
    return {};
  }

  std::vector<bool> visited(size / 4 + 1);
  std::unordered_set<std::string> names;
  std::vector<uint32_t> pending;
  uint32_t node = 0;
  bool have_node = true;
  for (;;) {
    while (have_node) {
      if (node > size || size - node < kEntryHeaderBytes) {
        return {Status::kCorrupt, "directory entry outside directory '" + prefix + "'"};
      }
      if (visited[node / 4]) {
        return {Status::kCorrupt, "directory entry reached twice in '" + prefix + "'"};
      }
      visited[node / 4] = true;
      pending.push_back(node);
      const uint16_t left = Le16(&data[node]);
      have_node = left != 0 && left != kPaddingChild;
      node = uint32_t(left) * 4;
    }
    if (pending.empty()) {
      return {};
    }
    node = pending.back();
    pending.pop_back();
    if (Result entry = Entry(data, node, prefix, depth, names); !entry.ok()) {
      return entry;
    }
    const uint16_t right = Le16(&data[node + 2]);
    have_node = right != 0 && right != kPaddingChild;
    node = uint32_t(right) * 4;
  }
}

Result TreeParser::Entry(const std::vector<uint8_t>& data, uint32_t node, const std::string& prefix, size_t depth,
                         std::unordered_set<std::string>& names) {
  const uint8_t* p = &data[node];
  const uint32_t sector = Le32(p + 4);
  const uint32_t size = Le32(p + 8);
  const uint8_t attributes = p[12];
  const size_t name_length = p[13];
  if (name_length == 0 || data.size() - node - kEntryHeaderBytes < name_length) {
    return {Status::kCorrupt, "malformed directory entry in '" + prefix + "'"};
  }
  const std::string_view name(reinterpret_cast<const char*>(p) + kEntryHeaderBytes, name_length);
  if (!IsSafeName(name)) {
    return {Status::kUnsafeName, "unsafe entry name '" + Printable(name) + "'"};
  }
  if (!names.insert(LowerAscii(name)).second) {
    return {Status::kUnsafeName, "names differ only in case: '" + Printable(name) + "'"};
  }
  if (++entries_ > kMaxEntries) {
    return {Status::kCorrupt, "too many directory entries"};
  }
  std::string path = prefix + std::string(name);
  if (path.size() > kMaxPathBytes) {
    return {Status::kCorrupt, "path too long: '" + Printable(path) + "'"};
  }
  if (attributes & kDirectoryAttribute) {
    if (depth + 1 > kMaxDepth) {
      return {Status::kCorrupt, "directories are nested too deeply at '" + Printable(path) + "'"};
    }
    return Directory(sector, size, path + "/", depth + 1);
  }
  files_.push_back({std::move(path), partition_offset_ + uint64_t(sector) * kSectorBytes, size});
  return {};
}

}  // namespace

bool IsSafeName(std::string_view name) {
  if (name.empty() || name == "." || name == "..") {
    return false;
  }
  for (const unsigned char c : name) {
    if (c < 0x20 || c >= 0x7F || std::strchr("\\/:*?\"<>|", c)) {
      return false;
    }
  }
  if (name.back() == ' ' || name.back() == '.') {
    return false;
  }
  return !IsReservedDeviceName(name);
}

Result Image::Open(const std::filesystem::path& file) {
  in_.close();
  in_.clear();
  files_.clear();
  index_.clear();
  image_size_ = 0;
  partition_offset_ = 0;

  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec)) {
    return {Status::kIoError, "not a regular file"};
  }
  const uintmax_t size = std::filesystem::file_size(file, ec);
  if (ec) {
    return {Status::kIoError, "cannot read the file size: " + ec.message()};
  }
  in_.open(file, std::ios::binary);
  if (!in_) {
    return {Status::kIoError, "cannot open the file"};
  }
  image_size_ = size;

  Volume volume;
  if (Result located = LocateVolume(*this, volume); !located.ok()) {
    return located;
  }
  partition_offset_ = volume.partition_offset;
  if (Result parsed = TreeParser(*this, partition_offset_, files_).Parse(volume.root_sector, volume.root_size);
      !parsed.ok()) {
    files_.clear();
    return parsed;
  }
  for (size_t i = 0; i < files_.size(); ++i) {
    index_.emplace(LowerAscii(files_[i].path), i);
  }
  return {};
}

const File* Image::Find(std::string_view path) const {
  const auto found = index_.find(LowerAscii(path));
  return found == index_.end() ? nullptr : &files_[found->second];
}

bool Image::ReadAt(uint64_t offset, void* out, size_t length) {
  if (offset > image_size_ || length > image_size_ - offset) {
    return false;
  }
  in_.clear();
  in_.seekg(std::streamoff(offset));
  in_.read(static_cast<char*>(out), std::streamsize(length));
  return in_.gcount() == std::streamsize(length);
}

}  // namespace crackdown::xdvdfs
