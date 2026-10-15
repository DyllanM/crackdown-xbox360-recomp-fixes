#!/usr/bin/env python3
"""List or extract selected files from the local Crackdown XDVDFS image."""

import argparse
import pathlib
import struct

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"


def volume(image):
    for base in (0, 0xFD90000):
        image.seek(base + 32 * SECTOR)
        header = image.read(32)
        if header.startswith(MAGIC):
            sector, size = struct.unpack_from("<II", header, 20)
            return base, sector, size
    raise ValueError("XDVDFS volume descriptor not found")


def entries(image, base, sector, size):
    image.seek(base + sector * SECTOR)
    data = image.read(size)
    if len(data) != size:
        raise ValueError("truncated directory")
    visited = set()

    def node(offset):
        if offset in visited or offset + 14 > size:
            return
        visited.add(offset)
        left, right, child_sector, child_size, flags, name_len = struct.unpack_from(
            "<HHIIBB", data, offset
        )
        if not name_len or offset + 14 + name_len > size:
            raise ValueError("invalid directory entry")
        name = data[offset + 14 : offset + 14 + name_len].decode("latin-1")
        if left:
            yield from node(left * 4)
        yield name, child_sector, child_size, flags
        if right:
            yield from node(right * 4)

    yield from node(0)


def walk(image, base, sector, size, prefix=""):
    for name, child_sector, child_size, flags in entries(image, base, sector, size):
        path = f"{prefix}{name}"
        if flags & 0x10:
            yield from walk(image, base, child_sector, child_size, path + "/")
        else:
            yield path, child_sector, child_size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=pathlib.Path)
    parser.add_argument("--match", default="", help="case-insensitive path substring")
    parser.add_argument("--extract", help="exact path to extract")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if args.extract and not args.output:
        parser.error("--extract requires --output")
    with args.image.open("rb") as image:
        base, sector, size = volume(image)
        found = False
        for path, file_sector, file_size in walk(image, base, sector, size):
            if args.extract:
                if path.lower() != args.extract.lower():
                    continue
                found = True
                args.output.parent.mkdir(parents=True, exist_ok=True)
                image.seek(base + file_sector * SECTOR)
                with args.output.open("wb") as output:
                    remaining = file_size
                    while remaining:
                        chunk = image.read(min(remaining, 1024 * 1024))
                        if not chunk:
                            raise ValueError("truncated file")
                        output.write(chunk)
                        remaining -= len(chunk)
                print(f"{path}: {file_size} bytes -> {args.output}")
                break
            if args.match.lower() in path.lower():
                print(f"{path}\t{file_size}")
                found = True
        if args.extract and not found:
            raise SystemExit(f"not found: {args.extract}")


if __name__ == "__main__":
    main()
