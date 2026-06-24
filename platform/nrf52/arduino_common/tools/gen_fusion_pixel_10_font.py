#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import argparse
import bisect
import sys


sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_fusion_pixel_12_font as base  # noqa: E402


base.FONT_ASCENT = 9
base.FONT_DESCENT = 1
base.CELL_WIDTH = 10
base.CELL_HEIGHT = 10


def write_header(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "#pragma once\n\n"
        "#include \"ui/mono/font/mono_font.h\"\n\n"
        "namespace ui::mono\n{\n"
        "// Generated from Fusion Pixel 10px monospaced zh_hans BDF for the NRF mono UI path.\n"
        "// Glyph bitmaps are normalized into fixed 10x10 cells so the compact renderer\n"
        "// can keep one raster contract for ASCII and CJK.\n"
        "extern const MonoFont kFusionPixel10Font;\n"
        "} // namespace ui::mono\n",
        encoding="utf-8",
    )


def write_source(path: Path, entries: list[base.Glyph], fallback_index: int) -> None:
    bitmap_body, cp_body, advance_body = base.format_bitmap(entries)
    path.parent.mkdir(parents=True, exist_ok=True)
    text = f"""#ifndef TRAIL_MATE_NRF_MONO_FUSION_PIXEL_10_ENABLED
#define TRAIL_MATE_NRF_MONO_FUSION_PIXEL_10_ENABLED 0
#endif

#if TRAIL_MATE_NRF_MONO_FUSION_PIXEL_10_ENABLED

#include "ui/fonts/fusion_pixel_10_font_generated.h"

namespace ui::mono
{{

static const uint8_t kFusionPixel10Bitmap[] = {{
{bitmap_body}
}};

static const uint16_t kFusionPixel10Codepoints[] = {{
{cp_body}
}};

static const uint8_t kFusionPixel10Advances[] = {{
{advance_body}
}};

const MonoFont kFusionPixel10Font = MonoFont::makeCompact16(
    kFusionPixel10Bitmap,
    kFusionPixel10Codepoints,
    kFusionPixel10Advances,
    static_cast<uint16_t>({len(entries)}),
    {base.CELL_HEIGHT},
    {base.FONT_ASCENT},
    {base.CELL_WIDTH},
    static_cast<uint16_t>({fallback_index}),
    {base.CELL_WIDTH},
    {base.CELL_HEIGHT},
    {base.CELL_WIDTH});

}} // namespace ui::mono

#endif
"""
    path.write_text(text, encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bdf", required=True)
    ap.add_argument("--out-header", required=True)
    ap.add_argument("--out-source", required=True)
    args = ap.parse_args()

    glyph_map, default_char = base.parse_bdf(Path(args.bdf))
    wanted = base.default_charset()
    if default_char not in glyph_map and ord("?") in glyph_map:
        default_char = ord("?")

    selected: list[base.Glyph] = []
    selected_codes: list[int] = []
    for cp in wanted:
        g = glyph_map.get(cp)
        if g is None:
            continue
        selected.append(g)
        selected_codes.append(cp)

    if default_char not in selected_codes and default_char in glyph_map:
        selected.append(glyph_map[default_char])
        selected_codes.append(default_char)

    order = sorted(range(len(selected_codes)), key=lambda idx: selected_codes[idx])
    selected = [selected[i] for i in order]
    selected_codes = [selected_codes[i] for i in order]
    fallback_index = bisect.bisect_left(selected_codes, default_char)
    if fallback_index >= len(selected_codes) or selected_codes[fallback_index] != default_char:
        fallback_index = 0

    write_header(Path(args.out_header))
    write_source(Path(args.out_source), selected, fallback_index)
    print(f"generated {len(selected)} glyphs from {args.bdf}")


if __name__ == "__main__":
    main()
