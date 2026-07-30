# Font Assets

This directory stores source vector fonts used to generate Trail Mate font assets.

## Noto Sans CJK SC

- File: `NotoSansCJKsc-Regular.otf`
- Source:
  `https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`
- License: SIL Open Font License 1.1 (OFL)

## Noto Naskh Arabic

- File: `NotoNaskhArabic-Regular.otf`
- Source:
  `https://raw.githubusercontent.com/googlefonts/noto-fonts/main/unhinted/otf/NotoNaskhArabic/NotoNaskhArabic-Regular.otf`
- License: SIL Open Font License 1.1 (OFL)

## Noto Emoji Monochrome

- File: `NotoEmoji-Regular.ttf`
- Source:
  `https://raw.githubusercontent.com/zjaco13/Noto-Emoji-Monochrome/main/fonts/NotoEmoji-Regular.ttf`
- Upstream project:
  `https://github.com/googlefonts/noto-emoji`
- License: SIL Open Font License 1.1 (OFL)

## Bitmap Compression Tool

Use the in-repo tool to convert an existing LVGL bitmap font (`lv_font_conv` output)
to LVGL compressed bitmap format (`bitmap_format = 2`):

```bash
./tools/compress_lvgl_bitmap_font.py path/to/generated_lvgl_font.c
```

The tool validates each glyph via encode/decode round-trip before writing.

## Built-In Emoji Catalogue

The reviewed 324-item Emoji catalogue is not an external pack. Its source of
truth is `tools/emoji_candidates_trailmate.json`; the generated C++ table and
16px/2bpp binfont are compiled into the firmware. The catalogue is grouped for
Pager use into Common, Radio, Nav, Weather, Survive, Rescue, Camp, People, and
Animals.

After changing the manifest, regenerate both the charset and the embedded font
from the repository root:

```bash
python tools/generate_builtin_emoji_data.py \
  --manifest tools/emoji_candidates_trailmate.json \
  --font tools/fonts/NotoEmoji-Regular.ttf \
  --charset-output <temporary charset.txt>

python tools/generate_binfont_with_lv_font_conv.py \
  --font tools/fonts/NotoEmoji-Regular.ttf \
  --charset-file <temporary charset.txt> \
  --output <temporary emoji.bin> \
  --size 16 --bpp 2

python tools/generate_builtin_emoji_data.py \
  --manifest tools/emoji_candidates_trailmate.json \
  --font tools/fonts/NotoEmoji-Regular.ttf \
  --binfont <temporary emoji.bin> \
  --output modules/ui_shared/src/ui/widgets/text_candidate_builtin_emoji_data.h
```

The generator rejects duplicate candidates, category count mismatches, an
unexpected total, and codepoints unavailable in `NotoEmoji-Regular.ttf`.

## External Pack Workflow

Simplified Chinese no longer ships as a compiled-in UI font. The repository now
expects Chinese glyph coverage to be generated into external font packs.

The reference bundle lives under:

```text
packs/zh-Hans
```

Typical flow:

1. Refresh the ranked Pinyin glyph sources:

```bash
python tools/extract_pinyin_chars.py
```

2. Generate the core pack subset:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/zh-Hans --font-pack-id zh-hans-core
```

3. Generate the extension pack subset:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/zh-Hans --font-pack-id zh-hans-ext
```

4. Generate `font.bin` files with `lv_font_conv` using:

- font source: `tools/fonts/NotoSansCJKsc-Regular.otf`
- glyph subsets:
  - `packs/zh-Hans/fonts/zh-hans-core/charset.txt`
  - `packs/zh-Hans/fonts/zh-hans-ext/charset.txt`
- output format: `bin`
- size: `16`
- bpp: `2`

5. Copy the pack directories onto the SD card under `/trailmate/packs/...`.
