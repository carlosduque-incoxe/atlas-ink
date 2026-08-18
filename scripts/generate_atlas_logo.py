#!/usr/bin/env python3
"""Generate the deterministic 120x120 monochrome Atlas Ink logo."""

from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
PNG_PATH = ROOT / "src/images/Logo120.png"
HEADER_PATH = ROOT / "src/images/Logo120.h"
SIZE = 120
SCALE = 4


def scaled(points):
    return [(x * SCALE, y * SCALE) for x, y in points]


def generate_image():
    image = Image.new("L", (SIZE * SCALE, SIZE * SCALE), 255)
    draw = ImageDraw.Draw(image)

    # Atlas/compass orbit. The open horizontal gaps keep the mark light and
    # prevent the ring from merging into the A at e-ink resolution.
    box = tuple(v * SCALE for v in (9, 9, 111, 111))
    draw.arc(box, 202, 338, fill=0, width=7 * SCALE)
    draw.arc(box, 22, 158, fill=0, width=7 * SCALE)

    # Strong geometric A. Its hollow counter resembles a page/ink nib.
    outer = [(60, 17), (103, 101), (89, 101), (78, 79), (42, 79), (31, 101), (17, 101)]
    draw.polygon(scaled(outer), fill=0)
    counter = [(60, 42), (73, 69), (47, 69)]
    draw.polygon(scaled(counter), fill=255)

    # Small north marker: atlas/compass cue, separated from the letterform.
    draw.polygon(scaled([(60, 5), (55, 13), (65, 13)]), fill=0)

    image = image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    # Hard 1-bit threshold for deterministic e-ink output.
    threshold = [0 if value < 128 else 255 for value in range(256)]
    return image.point(threshold, mode="1")


def pack_msb(image):
    data = bytearray()
    for y in range(SIZE):
        for byte_x in range(SIZE // 8):
            value = 0
            for bit in range(8):
                x = byte_x * 8 + bit
                value = (value << 1) | (1 if image.getpixel((x, y)) else 0)
            data.append(value)
    return bytes(data)


def write_header(data):
    assert len(data) == SIZE * SIZE // 8
    rows = []
    for offset in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset : offset + 16]))
    body = ",\n".join(rows)
    HEADER_PATH.write_text(
        "#pragma once\n"
        "#include <cstdint>\n\n"
        "// Atlas Ink logo. Image dimensions: 120 by 120, 1-bit MSB-first.\n"
        "static const uint8_t Logo120[] = {\n"
        f"{body}\n"
        "};\n",
        encoding="utf-8",
    )


def main():
    image = generate_image()
    data = pack_msb(image)
    PNG_PATH.parent.mkdir(parents=True, exist_ok=True)
    image.save(PNG_PATH)
    write_header(data)
    black = image.histogram()[0]
    assert 2500 <= black <= 6500, black
    print(f"png={PNG_PATH}")
    print(f"header={HEADER_PATH}")
    print(f"bytes={len(data)} black_pixels={black}")


if __name__ == "__main__":
    main()
