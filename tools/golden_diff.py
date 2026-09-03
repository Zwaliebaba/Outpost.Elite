#!/usr/bin/env python3
"""Show what changed between two golden canvases (slice 1d-c, Risk R10).

A golden that fails with a number and no picture is a golden nobody will re-record correctly,
so the test writes both images as PNGs and names this command. It prints where they differ and
writes a third PNG marking the differing pixels in white on the expected image.

    python tools/golden_diff.py expected.png actual.png [diff.png]

Reads the indexed PNGs the harness writes -- 320x200, eight-bit palette, stored deflate -- with
nothing but the standard library, because a golden diff that needs a dependency installed is one
more reason not to look at the picture.
"""

from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def read_png(_path: Path) -> tuple[int, int, bytes, bytes]:
    data = _path.read_bytes()
    if data[:8] != SIGNATURE:
        sys.exit(f"error: {_path} is not a PNG")

    chunks: dict[bytes, bytes] = {}
    offset = 8
    while offset < len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        kind = data[offset + 4 : offset + 8]
        body = data[offset + 8 : offset + 8 + length]
        chunks[kind] = chunks.get(kind, b"") + body
        offset += 12 + length

    width, height, depth, colour = struct.unpack(">IIBB", chunks[b"IHDR"][:10])
    if depth != 8 or colour != 3:
        sys.exit(f"error: {_path} is not an eight-bit indexed image")

    raw = zlib.decompress(chunks[b"IDAT"])
    stride = width + 1
    pixels = bytearray()
    for row in range(height):
        if raw[row * stride] != 0:
            sys.exit(f"error: {_path} uses a row filter this reader does not implement")
        pixels += raw[row * stride + 1 : (row + 1) * stride]

    return width, height, bytes(pixels), chunks[b"PLTE"]


def write_png(_path: Path, _width: int, _height: int, _pixels: bytes, _palette: bytes) -> None:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + _pixels[row * _width : (row + 1) * _width] for row in range(_height))
    out = SIGNATURE
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", _width, _height, 8, 3, 0, 0, 0))
    out += chunk(b"PLTE", _palette)
    out += chunk(b"IDAT", zlib.compress(raw, 9))
    out += chunk(b"IEND", b"")
    _path.write_bytes(out)


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    expected_path = Path(sys.argv[1])
    actual_path = Path(sys.argv[2])
    diff_path = Path(sys.argv[3]) if len(sys.argv) > 3 else actual_path.with_name(actual_path.stem + "-diff.png")

    width, height, expected, palette = read_png(expected_path)
    other_width, other_height, actual, _ = read_png(actual_path)

    if (width, height) != (other_width, other_height):
        sys.exit(f"error: {width}x{height} against {other_width}x{other_height} -- different sizes")

    differing = [index for index in range(len(expected)) if expected[index] != actual[index]]
    if not differing:
        print("identical")
        return 0

    # Index 1 is white in the harness's palette, which is what makes the marks readable.
    marked = bytearray(expected)
    for index in differing:
        marked[index] = 1
    write_png(diff_path, width, height, bytes(marked), palette)

    print(f"{len(differing)} of {len(expected)} pixels differ ({100.0 * len(differing) / len(expected):.2f}%)")

    rows = sorted({index // width for index in differing})
    columns = sorted({index % width for index in differing})
    print(f"  rows    {rows[0]}..{rows[-1]}")
    print(f"  columns {columns[0]}..{columns[-1]}")

    print("  first ten:")
    for index in differing[:10]:
        print(f"    ({index % width:3}, {index // width:3})  expected {expected[index]:2}  actual {actual[index]:2}")

    print(f"  marked image: {diff_path}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
