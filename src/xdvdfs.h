#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace crackdown::xdvdfs {

inline constexpr uint32_t kSectorBytes = 2048;
inline constexpr size_t kMaxDepth = 32;                          // directory levels below the root
inline constexpr size_t kMaxEntries = 0x40000;                   // files and directories together
inline constexpr uint64_t kMaxDirectoryBytes = 16ull << 20;      // all directories together
inline constexpr size_t kMaxPathBytes = 512;

enum class Status {
  kOk,
  kIoError,
  kNotAnImage,   // no volume descriptor at any known partition offset
  kTruncated,    // the volume or a directory extends past the end of the file
  kCorrupt,      // loops, revisited entries, limits exceeded, malformed entries
  kUnsafeName,   // an entry name that cannot be a single plain file name
};

struct Result {
  Status status = Status::kOk;
  std::string detail;  // for the log
  bool ok() const { return status == Status::kOk; }
};

struct File {
  std::string path;  // forward-slash separated, as stored in the image
  uint64_t offset = 0;  // absolute position in the image file
  uint64_t size = 0;
};

// A name that is one printable-ASCII path component: no separators, drive or stream markers, no reserved device
// name, no trailing dot or space.
bool IsSafeName(std::string_view name);

// A read-only Xbox 360 game disc image. Open reads and validates the whole directory tree; files are read on demand.
class Image {
 public:
  Result Open(const std::filesystem::path& file);

  uint64_t image_size() const { return image_size_; }
  uint64_t partition_offset() const { return partition_offset_; }
  const std::vector<File>& files() const { return files_; }

  // Case-insensitive; `path` uses forward slashes. Null when the image has no such file.
  const File* Find(std::string_view path) const;
  // True when the file's data lies inside the image file. A truncated image still lists the files it lost.
  bool Contains(const File& file) const {
    return file.size == 0 || (file.offset <= image_size_ && file.size <= image_size_ - file.offset);
  }
  // `offset` is absolute. False when the range leaves the image or the read fails.
  bool ReadAt(uint64_t offset, void* out, size_t length);

 private:
  std::ifstream in_;
  uint64_t image_size_ = 0;
  uint64_t partition_offset_ = 0;
  std::vector<File> files_;
  std::unordered_map<std::string, size_t> index_;
};

}  // namespace crackdown::xdvdfs
