#!/usr/bin/env python3
"""Decode the 8-bit tiled EnforcerRatingIcons texture from a local TPK."""

import argparse
import pathlib
import struct
import zlib


def tiled_offset(x, y, pitch):
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << 7
    micro = (x & 7) + ((y & 0xE) << 2)
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F)


def chunk(tag, body):
    return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))


def png(path, width, height, pixels):
    rows = b"".join(b"\0" + pixels[y * width : (y + 1) * width] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tpk", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    data = args.tpk.read_bytes()
    count, names_size, texture_bytes = struct.unpack_from(">III", data)
    names_start = 12 + 4 * count
    names = []
    for index in range(count):
        offset = struct.unpack_from(">I", data, 12 + 4 * index)[0]
        names.append(data[names_start + offset :].split(b"\0", 1)[0].decode("ascii"))
    index = names.index("EnforcerRatingIcons")
    descriptor = names_start + names_size + index * 52
    word0, word1, word2 = struct.unpack_from(">III", data, descriptor + 28)
    width, height = (word2 & 0x1FFF) + 1, ((word2 >> 13) & 0x1FFF) + 1
    pitch = ((word0 >> 22) & 0x1FF) * 32
    if word1 & 63 != 2 or not (word0 >> 31) or pitch < width:
        raise ValueError("unexpected Enforcer texture format")
    base = len(data) - texture_bytes + ((word1 >> 12) << 12)
    pixels = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            pixels[y * width + x] = data[base + tiled_offset(x, y, pitch)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    png(args.output, width, height, pixels)
    selected = b"".join(pixels[cell * width * width : (cell + 1) * width * width] for cell in (0, 17, 13))
    png(args.output.with_name("EnforcerRatingIcons_0_17_13.png"), width, width * 3, selected)
    print(f"{names[index]}: {width}x{height}, 8-bit tiled, pitch={pitch}, file_offset=0x{base:X}")


if __name__ == "__main__":
    main()
