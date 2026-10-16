#!/usr/bin/env python3
"""Generate or verify src/game_manifest.h, the table of files a supported Crackdown install holds.

  python -I tools/make-game-manifest.py <disc image or extracted folder>
  python -I tools/make-game-manifest.py --verify <disc image or extracted folder>

The source is either an Xbox 360 disc image (XDVDFS) or a folder with the extracted disc contents.
Every file except the optional prefixes is required. The executable is pinned by size and SHA-256.
"""

import argparse
import difflib
import hashlib
import pathlib
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
PARTITION_OFFSETS = (0, 0xFD90000, 0x2090000, 0x18300000)
MAX_DEPTH = 32
MAX_ENTRIES = 0x40000
CHUNK = 1 << 20

XEX_NAME = "default.xex"
SUPPORTED_XEX_SHA256 = "7AAA7A8920D1C3C737E58CF583DEBAFE6013BE0A96BEC3EC136E5DE7E3377E5A"
OPTIONAL_PREFIXES = ("$SystemUpdate/",)
DEFAULT_OUTPUT = pathlib.Path(__file__).resolve().parent.parent / "src" / "game_manifest.h"
COMMAND = "python -I tools/make-game-manifest.py"


class ManifestError(Exception):
    pass


def safe_name(name):
    if not name or name in (".", "..") or name[-1] in " .":
        return False
    return all(0x20 <= ord(c) < 0x7F and c not in '\\/:*?"<>|' for c in name)


class ImageSource:
    def __init__(self, path):
        self.file = open(path, "rb")
        self.file.seek(0, 2)
        self.size = self.file.tell()
        self.base, root_sector, root_size = self._volume()
        self.entries = {}
        self._visited = set()
        self._count = 0
        self._walk(root_sector, root_size, "", 0)

    def close(self):
        self.file.close()

    def _read(self, offset, length):
        self.file.seek(offset)
        data = self.file.read(length)
        if len(data) != length:
            raise ManifestError("the image is truncated")
        return data

    def _volume(self):
        for base in PARTITION_OFFSETS:
            at = base + 32 * SECTOR
            if at + SECTOR > self.size:
                continue
            sector = self._read(at, SECTOR)
            if sector[:20] != MAGIC or sector[0x7EC:0x800] != MAGIC:
                continue
            root_sector, root_size = struct.unpack_from("<II", sector, 20)
            if base + root_sector * SECTOR + root_size <= self.size:
                return base, root_sector, root_size
        raise ManifestError("no XDVDFS volume descriptor found")

    def _walk(self, sector, size, prefix, depth):
        if size == 0:
            return
        if depth > MAX_DEPTH:
            raise ManifestError("directories are nested too deeply")
        if sector in self._visited:
            raise ManifestError("a directory is reached twice")
        self._visited.add(sector)
        data = self._read(self.base + sector * SECTOR, size)
        if data[:4] == b"\xff\xff\xff\xff":
            return
        seen = set()
        names = set()
        stack = []
        node, have = 0, True
        while True:
            while have:
                if node + 14 > size or node in seen:
                    raise ManifestError("invalid directory entry")
                seen.add(node)
                stack.append(node)
                left = struct.unpack_from("<H", data, node)[0]
                have, node = left not in (0, 0xFFFF), left * 4
            if not stack:
                break
            node = stack.pop()
            self._entry(data, node, prefix, depth, names)
            right = struct.unpack_from("<H", data, node + 2)[0]
            have, node = right not in (0, 0xFFFF), right * 4

    def _entry(self, data, node, prefix, depth, names):
        _, _, sector, length, flags, name_len = struct.unpack_from("<HHIIBB", data, node)
        if not name_len or node + 14 + name_len > len(data):
            raise ManifestError("invalid directory entry")
        name = data[node + 14 : node + 14 + name_len].decode("latin-1")
        if not safe_name(name):
            raise ManifestError(f"unsafe entry name {name!r}")
        if name.lower() in names:
            raise ManifestError(f"names differ only in case: {name!r}")
        names.add(name.lower())
        self._count += 1
        if self._count > MAX_ENTRIES:
            raise ManifestError("too many entries")
        path = prefix + name
        if flags & 0x10:
            self._walk(sector, length, path + "/", depth + 1)
        else:
            self.entries[path] = (self.base + sector * SECTOR, length)

    def files(self):
        return {path: size for path, (_, size) in self.entries.items()}

    def chunks(self, path):
        offset, length = self.entries[path]
        if offset + length > self.size:
            raise ManifestError(f"{path} lies beyond the end of the image")
        self.file.seek(offset)
        while length:
            data = self.file.read(min(length, CHUNK))
            if not data:
                raise ManifestError(f"{path} is truncated")
            length -= len(data)
            yield data


class FolderSource:
    def __init__(self, root):
        self.root = root
        self.paths = {}
        lowered = set()
        for item in sorted(root.rglob("*")):
            if item.is_file():
                relative = item.relative_to(root).as_posix()
                if relative.lower() in lowered:
                    raise ManifestError(f"names differ only in case: {relative}")
                lowered.add(relative.lower())
                self.paths[relative] = item

    def close(self):
        pass

    def files(self):
        return {path: item.stat().st_size for path, item in self.paths.items()}

    def chunks(self, path):
        with self.paths[path].open("rb") as handle:
            while data := handle.read(CHUNK):
                yield data


def open_source(path):
    if path.is_dir():
        return FolderSource(path)
    if path.is_file():
        return ImageSource(path)
    raise ManifestError(f"{path} is neither a disc image nor a folder")


def is_optional(path):
    return any(path.lower().startswith(prefix.lower()) for prefix in OPTIONAL_PREFIXES)


def collect(source):
    files = source.files()
    for path in files:
        if not all(safe_name(part) for part in path.split("/")):
            raise ManifestError(f"unsafe path {path!r}")
    required = sorted((path, size) for path, size in files.items() if not is_optional(path))
    xex = [path for path, _ in required if path.lower() == XEX_NAME]
    if len(xex) != 1:
        raise ManifestError(f"the source has no {XEX_NAME} at its root")
    digest = hashlib.sha256()
    for data in source.chunks(xex[0]):
        digest.update(data)
    if digest.hexdigest().upper() != SUPPORTED_XEX_SHA256:
        raise ManifestError(
            f"{xex[0]} has SHA-256 {digest.hexdigest().upper()}, not the supported {SUPPORTED_XEX_SHA256}"
        )
    return required, dict(required)[xex[0]], digest.digest()


def render(required, xex_size, xex_digest):
    lines = [
        f"// Generated by: {COMMAND} <disc image or extracted folder>",
        f"// Check with:   {COMMAND} --verify <disc image or extracted folder>",
        "// Do not edit by hand: the installer and the start-up check validate the game files against this table.",
        f"// {len(required)} required files, {sum(size for _, size in required)} bytes.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace crackdown::manifest {",
        "",
        f'inline constexpr char kXexName[] = "{XEX_NAME}";',
        f"inline constexpr uint64_t kXexSize = {xex_size};",
        f"// SHA-256 {xex_digest.hex().upper()}",
        "inline constexpr std::array<uint8_t, 32> kXexSha256 = {",
    ]
    for start in range(0, 32, 8):
        lines.append("    " + ", ".join(f"0x{byte:02X}" for byte in xex_digest[start : start + 8]) + ",")
    lines += [
        "};",
        "",
        "struct RequiredFile {",
        "  const char* path;",
        "  uint64_t size;",
        "};",
        "",
        "inline constexpr RequiredFile kRequiredFiles[] = {",
    ]
    lines += [f'    {{ "{path}", {size} }},' for path, size in required]
    lines += [
        "};",
        "",
        "// Present on the disc, never required and never installed.",
        "inline constexpr const char* kOptionalPrefixes[] = {",
    ]
    lines += [f'    "{prefix}",' for prefix in OPTIONAL_PREFIXES]
    lines += ["};", "", "}  // namespace crackdown::manifest", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=pathlib.Path, help="disc image (.iso) or extracted disc folder")
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT, help="header to write or verify")
    parser.add_argument("--verify", action="store_true", help="compare the header with the source; write nothing")
    args = parser.parse_args()
    try:
        source = open_source(args.source)
        try:
            required, xex_size, xex_digest = collect(source)
        finally:
            source.close()
    except (ManifestError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    text = render(required, xex_size, xex_digest)

    if not args.verify:
        with args.output.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        print(f"wrote {args.output}: {len(required)} required files, xex {xex_size} bytes")
        return 0

    try:
        current = args.output.read_bytes().decode("utf-8")
    except OSError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    if current == text:
        print(f"OK: {args.output} matches the source ({len(required)} required files)")
        return 0
    diff = difflib.unified_diff(
        current.splitlines(), text.splitlines(), "header", "source", lineterm="", n=0
    )
    for count, line in enumerate(diff):
        if count >= 40:
            print("...")
            break
        print(line)
    print(f"MISMATCH: {args.output} differs from the source", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
