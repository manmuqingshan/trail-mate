#!/usr/bin/env python3
"""Convert intentionally authored raster artwork to an LVGL v9 descriptor.

The legacy default is RGB565A8.  ``--format i1`` is deliberately restricted
to already-authored transparent-black artwork; it does not convert, threshold,
dither, or otherwise derive an EPD asset from a colour source image.  This
keeps the small-display artwork process explicit and repeatable.

Usage:
    python png_to_lvgl.py <input.png> <output.c> [image_name]
    python png_to_lvgl.py <input.png> <output.inc> icon --format i1 \
        --mono-output images/tdeckpro_epd/icon.png
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


RGB565A8 = "rgb565a8"
I1 = "i1"


def rgb565(red: int, green: int, blue: int) -> int:
    """Convert an RGB888 color to RGB565."""
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def load_rgba(path: Path) -> Image.Image:
    """Load a source image as an RGBA image without changing its dimensions."""
    with Image.open(path) as source:
        return source.convert("RGBA")


def require_authored_black_ink(image: Image.Image) -> None:
    """Reject accidental attempts to convert a colour image to EPD artwork."""
    for red, green, blue, alpha in image.convert("RGBA").getdata():
        if alpha >= 128 and (red, green, blue) != (0, 0, 0):
            raise ValueError(
                "I1 output requires authored black/transparent artwork; "
                "redraw the icon for the target EPD instead of converting a colour image"
            )


def save_mono_png(image: Image.Image, output_path: Path) -> None:
    """Save a transparent black two-entry palette PNG.

    The saved PNG is genuinely 1 bit per pixel: palette entry zero is fully
    transparent and entry one is opaque black.  It can be inspected directly
    and is byte-for-byte equivalent to the bitmap packed for LVGL I1 below.
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)
    alpha = image.convert("RGBA").getchannel("A")
    data = bytes(1 if opacity >= 128 else 0 for opacity in alpha.getdata())
    indexed = Image.frombytes("P", image.size, data)
    indexed.putpalette([0, 0, 0, 0, 0, 0])
    indexed.save(output_path, transparency=0, bits=1)


def image_data_rgb565a8(image: Image.Image) -> tuple[bytes, int]:
    """Return LVGL RGB565A8 plane data and its RGB565 stride."""
    rgba = image.convert("RGBA")
    width, _ = rgba.size
    color_data: list[int] = []
    alpha_data: list[int] = []

    for red, green, blue, alpha in rgba.getdata():
        value = rgb565(red, green, blue)
        color_data.extend((value & 0xFF, (value >> 8) & 0xFF))
        alpha_data.append(alpha)

    return bytes(color_data + alpha_data), width * 2


def image_data_i1(image: Image.Image) -> tuple[bytes, int]:
    """Pack a transparent-black image for ``LV_COLOR_FORMAT_I1``.

    LVGL indexed images begin with a 32-bit BGRA palette.  Entry 0 is
    transparent black and entry 1 opaque black.  Pixel bits are MSB-first,
    matching the format emitted by LVGL's image converter.
    """
    rgba = image.convert("RGBA")
    width, height = rgba.size
    stride = (width + 7) // 8
    bits = bytearray(stride * height)
    alpha = rgba.getchannel("A")

    for y in range(height):
        row_start = y * width
        for x in range(width):
            if alpha.getpixel((x, y)) >= 128:
                bits[y * stride + x // 8] |= 0x80 >> (x % 8)

    transparent_black = bytes((0x00, 0x00, 0x00, 0x00))
    opaque_black = bytes((0x00, 0x00, 0x00, 0xFF))
    return transparent_black + opaque_black + bytes(bits), stride


def lvgl_preamble(image_name: str) -> list[str]:
    attr_name = image_name.upper()
    if attr_name.startswith("IMG_"):
        attr_name = attr_name[4:]

    return [
        "#ifdef __has_include",
        "    #if __has_include(\"lvgl.h\")",
        "        #ifndef LV_LVGL_H_INCLUDE_SIMPLE",
        "            #define LV_LVGL_H_INCLUDE_SIMPLE",
        "        #endif",
        "    #endif",
        "#endif",
        "",
        "#if defined(LV_LVGL_H_INCLUDE_SIMPLE)",
        "    #include \"lvgl.h\"",
        "#else",
        "    #include \"lvgl/lvgl.h\"",
        "#endif",
        "",
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
        "#define LV_ATTRIBUTE_MEM_ALIGN",
        "#endif",
        "",
        f"#ifndef LV_ATTRIBUTE_IMAGE_{attr_name}",
        f"#define LV_ATTRIBUTE_IMAGE_{attr_name}",
        "#endif",
        "",
    ]


def write_lvgl_image(image: Image.Image, output_path: Path, image_name: str, color_format: str) -> int:
    """Write *image* as a complete LVGL v9 C descriptor and return data size."""
    width, height = image.size
    if color_format == RGB565A8:
        data, stride = image_data_rgb565a8(image)
        lvgl_format = "LV_COLOR_FORMAT_RGB565A8"
    elif color_format == I1:
        data, stride = image_data_i1(image)
        lvgl_format = "LV_COLOR_FORMAT_I1"
    else:
        raise ValueError(f"unsupported color format: {color_format}")

    attr_name = image_name.upper()
    if attr_name.startswith("IMG_"):
        attr_name = attr_name[4:]

    lines = lvgl_preamble(image_name)
    lines.extend(
        [
            f"const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST "
            f"LV_ATTRIBUTE_IMAGE_{attr_name} uint8_t {image_name}_map[] = {{",
        ]
    )
    for offset in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset : offset + 16]) + ",")
    lines.extend(
        [
            "};",
            "",
            f"const lv_image_dsc_t {image_name} = {{",
            "    .header.magic = LV_IMAGE_HEADER_MAGIC,",
            f"    .header.cf = {lvgl_format},",
            "    .header.flags = 0,",
            f"    .header.w = {width},",
            f"    .header.h = {height},",
            f"    .header.stride = {stride},",
            f"    .data_size = sizeof({image_name}_map),",
            f"    .data = {image_name}_map,",
            "};",
            "",
        ]
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")
    return len(data)


def convert_png_to_lvgl(
    input_path: str | Path,
    output_path: str | Path,
    image_name: str = "img",
    color_format: str = RGB565A8,
    mono_output: str | Path | None = None,
) -> int:
    """Convert a PNG to an LVGL descriptor and return the generated byte size."""
    source = load_rgba(Path(input_path))
    if color_format == I1:
        require_authored_black_ink(source)
        if mono_output is not None:
            save_mono_png(source, Path(mono_output))
    elif mono_output is not None:
        raise ValueError("--mono-output requires --format i1")
    return write_lvgl_image(source, Path(output_path), image_name, color_format)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="source PNG")
    parser.add_argument("output", type=Path, help="destination C or include file")
    parser.add_argument("image_name", nargs="?", default="img", help="LVGL image descriptor symbol")
    parser.add_argument(
        "--format",
        choices=(RGB565A8, I1),
        default=RGB565A8,
        help="LVGL color format; defaults to the legacy RGB565A8 output",
    )
    parser.add_argument("--mono-output", type=Path, help="write the generated 1-bit PNG alongside the C output")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.input.is_file():
        raise SystemExit(f"Error: input file '{args.input}' not found")
    try:
        data_size = convert_png_to_lvgl(
            args.input,
            args.output,
            args.image_name,
            args.format,
            args.mono_output,
        )
    except ValueError as error:
        raise SystemExit(f"Error: {error}") from error
    with Image.open(args.input) as image:
        print(f"Converted {args.input} -> {args.output}: {image.width}x{image.height}, {data_size} bytes")


if __name__ == "__main__":
    main()
