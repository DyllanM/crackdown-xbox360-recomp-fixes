#!/usr/bin/env python3
"""Extract selected resources from Crackdown's zlib-compressed global DFF."""

import argparse
import pathlib
import struct
import zlib


def resource(data, filename):
    name = filename.encode("ascii") + b"\0"
    position = 0
    while (position := data.find(name, position)) >= 0:
        start = position - 20
        if data[start : start + 4] != b"\x16\x07\0\0":
            position += 1
            continue
        record_size = struct.unpack_from("<I", data, start + 4)[0]
        if struct.unpack_from("<I", data, start + 16)[0] != len(name):
            position += 1
            continue
        end = start + 12 + record_size
        cursor = position + len(name) + 16
        fields = []
        for _ in range(3):
            field_size = struct.unpack_from("<I", data, cursor)[0]
            cursor += 4
            fields.append(data[cursor : cursor + field_size - 1].decode("latin-1"))
            cursor += field_size
        blob_size = struct.unpack_from("<I", data, cursor)[0]
        cursor += 4
        if cursor + blob_size != end:
            raise ValueError(f"resource boundary mismatch: {filename}")
        return fields, data[cursor:end]
    raise KeyError(filename)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dff", type=pathlib.Path)
    parser.add_argument("filename")
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    fields, blob = resource(zlib.decompress(args.dff.read_bytes()), args.filename)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    print(f"{args.filename}: {len(blob)} bytes, type={fields[0]}, source={fields[1]}")


if __name__ == "__main__":
    main()
