# Europe Cyrillic-Extended Pack Bundle

This bundle keeps Russian support external to the firmware image.

It contains:

- locale pack: `ru`
- font pack: `cyrillic-eu`
- IME pack: `ru-cyrillic-keyboard`

The Russian translations were contributed by polarikus.

## Credits

Russian translations were contributed by polarikus, based on the
polarikus/trail-mate Russian localization PR:

https://github.com/polarikus/trail-mate/pull/1

The locale manifest points to:

- `ui_font_pack=cyrillic-eu`
- `content_font_pack=cyrillic-eu`
- `ime_pack=ru-cyrillic-keyboard`

Bundle-level package metadata lives in:

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `fonts/cyrillic-eu/`
  - `manifest.ini`
  - `build.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `locales/ru/`
  - `manifest.ini`
  - `strings.tsv`
- `ime/ru-cyrillic-keyboard/`
  - `manifest.ini`

## Generate The Font Pack

1. Refresh the subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/europe-cyrillic-ext --font-pack-id cyrillic-eu
```

2. Generate `font.bin`:

```powershell
python tools/generate_binfont_with_lv_font_conv.py `
  --font tools/fonts/NotoSansCJKsc-Regular.otf `
  --charset-file packs/europe-cyrillic-ext/fonts/cyrillic-eu/charset.txt `
  --output packs/europe-cyrillic-ext/fonts/cyrillic-eu/font.bin `
  --size 16 `
  --bpp 2 `
  --node-exe C:\Users\VicLi\AppData\Local\nodejs22\current\node.exe `
  --no-compress
```

Notes:

- The Russian locale is still in `translation_status=review`, but the bundle now
  declares the real `ru-cyrillic-keyboard` IME pack.
- `ru-cyrillic-keyboard` is a direct virtual-keyboard layout backed by
  `backend=builtin-keyboard-layout` and `layout=ru-cyrillic`; it is not a
  candidate-conversion engine and does not remap physical keyboard hardware.
- `build.ini` is the source-of-truth for generating `font.bin` during Pages/package builds.
- `font.bin` is ignored by Git. Regenerate it whenever the subset changes.
- `ranges.txt` and `estimated_ram_bytes` let the runtime decide whether the pack fits the active memory profile.
- The locale string table is structurally complete, but the locale remains in `translation_status=review` until native-language review is complete.

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/europe-cyrillic-ext-1.2.0.zip`
- `site/data/packs.json`

The zip is the bundle artifact for a future Extensions downloader. Its `payload/` directory unpacks into `/trailmate/packs/...`.

## Copy To SD Card

Copy the bundle contents so the SD card ends up with:

```text
/trailmate/packs/fonts/cyrillic-eu/manifest.ini
/trailmate/packs/fonts/cyrillic-eu/ranges.txt
/trailmate/packs/fonts/cyrillic-eu/font.bin
/trailmate/packs/locales/ru/manifest.ini
/trailmate/packs/locales/ru/strings.tsv
/trailmate/packs/ime/ru-cyrillic-keyboard/manifest.ini
```

After reboot, `Русский` appears in Settings only after the locale is promoted to `translation_status=release`. While it is in `review`, the runtime can install and index the pack but will not offer it as a selectable UI language.
