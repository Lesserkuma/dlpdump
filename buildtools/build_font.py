#!/usr/bin/env python3
"""Build a C font module from editable bitmap and TSV font assets."""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path


def parse_font(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, value = line.split("=", 1)
        out[key.strip()] = value.strip()
    return out


def parse_codepoint(text: str) -> int:
    if not re.fullmatch(r"U\+[0-9a-fA-F]{4,6}", text):
        raise ValueError(f"invalid codepoint {text!r}")
    value = int(text[2:], 16)
    if value > 0x10FFFF or 0xD800 <= value <= 0xDFFF:
        raise ValueError(f"unsupported codepoint {text!r}")
    return value


def codepoint_from_character(path: Path, lineno: int, text: str) -> int:
    if len(text) != 1:
        raise ValueError(f"{path}:{lineno}: character field must contain exactly one UTF-8 character")
    value = ord(text)
    if value > 0x10FFFF or 0xD800 <= value <= 0xDFFF:
        raise ValueError(f"{path}:{lineno}: unsupported character U+{value:04X}")
    return value


def parse_glyphs(path: Path) -> list[dict[str, int]]:
    glyphs: list[dict[str, int]] = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line:
            continue
        if line == "character\tadvance\tleft":
            continue
        fields = line.split("\t")
        if len(fields) != 3:
            raise ValueError(f"{path}:{lineno}: expected tab-separated fields: character, advance, left")
        codepoint = codepoint_from_character(path, lineno, fields[0])
        advance = int(fields[1], 10)
        left = int(fields[2], 10)
        glyphs.append({"codepoint": codepoint, "advance": advance, "left": left})
    return glyphs


def parse_int_field(font: dict[str, str], key: str) -> int:
    try:
        return int(font[key], 10)
    except KeyError as exc:
        raise ValueError(f"missing font config key {key!r}") from exc
    except ValueError as exc:
        raise ValueError(f"font config key {key!r} must be an integer") from exc


def validate_font_config(cell_w: int, cell_h: int, tracking: int,
                         columns: int, glyphs: list[dict[str, int]]) -> None:
    if cell_w <= 0 or cell_h <= 0:
        raise ValueError("cell_width and cell_height must be positive")
    if cell_w > 255 or cell_h > 255:
        raise ValueError("cell_width and cell_height must fit in uint8_t")
    if columns <= 0:
        raise ValueError("columns must be positive")
    if tracking < -cell_w or tracking > cell_w or tracking < -128 or tracking > 127:
        raise ValueError("tracking must fit in int8_t and stay within one cell width")
    if not glyphs:
        raise ValueError("glyph list must not be empty")
    if len(glyphs) > 0xffff:
        raise ValueError("glyph list must fit in uint16_t")
    bytes_per_glyph = ((cell_w * cell_h) + 7) // 8
    if bytes_per_glyph > 255:
        raise ValueError("packed glyph size must fit in uint8_t")
    for idx, glyph in enumerate(glyphs):
        if glyph["codepoint"] > 0xffff:
            raise ValueError(f"glyph {idx}: codepoint exceeds renderer limit U+FFFF")


def asset_hint(path: Path) -> str:
    asset_dir = path.parent.resolve()
    try:
        root = Path(__file__).resolve().parents[1]
        return asset_dir.relative_to(root).as_posix()
    except ValueError:
        return asset_dir.as_posix()


def read_bmp(path: Path) -> tuple[int, int, list[int]]:
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError("bitmap must be a BMP file")
    pixel_off = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError("unsupported BMP DIB header")
    width, height, planes, bpp, compression = struct.unpack_from("<iiHHI", data, 18)
    if planes != 1 or bpp not in (24, 32) or compression != 0:
        raise ValueError("BMP must be uncompressed 24-bit or 32-bit")
    top_down = height < 0
    height = abs(height)
    row_bytes = ((width * (bpp // 8) + 3) // 4) * 4
    pixels = [0] * (width * height)
    for y in range(height):
        src_y = y if top_down else height - 1 - y
        row = pixel_off + src_y * row_bytes
        for x in range(width):
            b, g, r = data[row + x * (bpp // 8):row + x * (bpp // 8) + 3]
            pixels[y * width + x] = 1 if (r + g + b) < (3 * 128) else 0
    return width, height, pixels


def group_ranges(glyphs: list[dict[str, int]]) -> tuple[list[tuple[int, int, int]], list[int], list[int]]:
    ranges: list[tuple[int, int, int]] = []
    used = [False] * len(glyphs)
    seen_codepoints: set[int] = set()

    for idx, glyph in enumerate(glyphs):
        codepoint = glyph["codepoint"]
        if codepoint in seen_codepoints:
            raise ValueError(f"glyph {idx}: duplicate codepoint U+{codepoint:04X}")
        seen_codepoints.add(codepoint)

    i = 0
    while i < len(glyphs):
        start_cp = glyphs[i]["codepoint"]
        j = i + 1
        while j < len(glyphs) and glyphs[j]["codepoint"] == start_cp + (j - i):
            j += 1
        if j - i >= 2:
            ranges.append((start_cp, glyphs[j - 1]["codepoint"], i))
            for k in range(i, j):
                used[k] = True
        i = j

    direct_pairs: list[tuple[int, int]] = []
    for idx, glyph in enumerate(glyphs):
        if used[idx]:
            continue
        direct_pairs.append((glyph["codepoint"], idx))
    direct_pairs.sort(key=lambda pair: pair[0])
    direct = [codepoint for codepoint, _glyph in direct_pairs]
    direct_glyphs = [glyph for _codepoint, glyph in direct_pairs]
    return ranges, direct, direct_glyphs


def c_array(name: str, ctype: str, values: list[int], per_line: int = 16) -> str:
    lines = [f"static const {ctype} {name}[] = {{"]
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        lines.append("    " + ", ".join(f"0x{v:02x}" if ctype == "uint8_t" else str(v) for v in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", required=True)
    parser.add_argument("--glyphs", required=True)
    parser.add_argument("--bitmap", required=True)
    parser.add_argument("--out-c", required=True)
    parser.add_argument("--out-h", required=True)
    args = parser.parse_args()

    font = parse_font(Path(args.font))
    glyphs = parse_glyphs(Path(args.glyphs))
    cell_w = parse_int_field(font, "cell_width")
    cell_h = parse_int_field(font, "cell_height")
    tracking = parse_int_field(font, "tracking")
    columns = parse_int_field(font, "columns")
    fallback_cp = parse_codepoint(font.get("fallback", "U+003F"))
    validate_font_config(cell_w, cell_h, tracking, columns, glyphs)
    width, height, pixels = read_bmp(Path(args.bitmap))
    rows = (len(glyphs) + columns - 1) // columns
    if width != columns * cell_w or height < rows * cell_h:
        raise ValueError("bitmap dimensions do not match font grid")

    fallback = next((i for i, g in enumerate(glyphs) if g["codepoint"] == fallback_cp), 0)
    bytes_per_glyph = ((cell_w * cell_h) + 7) // 8
    bits: list[int] = []
    metrics: list[int] = []
    for idx, glyph in enumerate(glyphs):
        left = glyph["left"]
        advance = glyph["advance"]
        if left < 0 or advance < 0 or left > cell_w or left + advance > cell_w:
            raise ValueError(f"glyph {idx}: invalid metrics")
        metrics.extend([left, advance])
        gx = (idx % columns) * cell_w
        gy = (idx // columns) * cell_h
        packed = [0xff] * bytes_per_glyph
        for y in range(cell_h):
            for x in range(cell_w):
                if pixels[(gy + y) * width + gx + x]:
                    bit = y * cell_w + x
                    packed[bit >> 3] &= ~(1 << (bit & 7))
        bits.extend(packed)

    ranges, direct, direct_glyphs = group_ranges(glyphs)
    out_c = Path(args.out_c)
    out_h = Path(args.out_h)
    out_c.parent.mkdir(parents=True, exist_ok=True)
    out_h.parent.mkdir(parents=True, exist_ok=True)
    generated_note = f"/* Generated by buildtools/build_font.py; edit {asset_hint(Path(args.font))} instead. */"

    range_lines = ["static const FontRange font_8x8_ranges[] = {"]
    for start, end, first in ranges:
        range_lines.append(f"    {{ 0x{start:04x}, 0x{end:04x}, {first} }},")
    range_lines.append("};")

    c_text = "\n\n".join([
        f"{generated_note}\n#include \"font.h\"\n",
        c_array("font_8x8_bits", "uint8_t", bits),
        c_array("font_8x8_metrics", "uint8_t", metrics),
        c_array("font_8x8_direct", "uint16_t", direct, 12),
        c_array("font_8x8_direct_glyphs", "uint16_t", direct_glyphs, 12),
        "\n".join(range_lines),
        "\n".join([
            "const Font font_8x8 = {",
            f"    {cell_w}, {cell_h}, {bytes_per_glyph}, {tracking},",
            f"    {len(glyphs)}, {fallback},",
            "    font_8x8_metrics,",
            "    font_8x8_bits,",
            "    font_8x8_ranges,",
            f"    {len(ranges)},",
            "    font_8x8_direct,",
            "    font_8x8_direct_glyphs,",
            f"    {len(direct)}",
            "};",
            "",
        ]),
    ])
    out_c.write_text(c_text, encoding="utf-8")
    out_h.write_text(
        f"{generated_note}\n"
        "#pragma once\n#include \"font.h\"\nextern const Font font_8x8;\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, struct.error) as exc:
        print(f"{Path(__file__).name}: error: {exc}", file=sys.stderr)
        raise SystemExit(1)
