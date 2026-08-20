# Locale, Font and IME Pack

This document explains the pack mechanism and packaging details.
The normative specification for the entire localization system is now located in
[`docs/specification/LOCALIZATION_SPEC.md`](./LOCALIZATION_SPEC.md).
the runtime owner general boundary
[`docs/specification/RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md`](./RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md).
If there is a conflict between these documents, `RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md` and
`LOCALIZATION_SPEC.md` take precedence.
For language pack packaging, release, version, archive and catalog update rules, please see
[`docs/specification/LOCALE_PACK_RELEASE_SPEC.md`](./LOCALE_PACK_RELEASE_SPEC.md).

## Goal

The pack system exists to solve four problems at the same time:

1. Keep the firmware image as small as possible. English remains built-in, and large script resources are moved out of the firmware image and into external pack storage.
2. Clearly distinguish between `installed` and `loaded`. Just because a pack appears on Flash or SD does not mean its fonts have been loaded into RAM.
3. Let mixed language content render correctly. Even if the UI is in Spanish, the system should still be able to display Chinese contact names as long as the corresponding font pack is installed.
4. Set boundaries on RAM usage to accommodate widely varying devices, including PSRAM-less targets.

This is already a set of first-class citizen runtime architecture, rather than several text-based bypass splicing.

## Core Model

Trail Mate explicitly models localization as three types of packs:

- `Locale Pack`
 Has a translated UI string and declares the UI font pack, content font pack, and optional IME pack it depends on.
- `Font Pack`
 Has glyph override metadata as well as external `font.bin` resources.
- `IME Pack`
 declares an input behavior, such as Simplified Chinese Pinyin, or directly submits the keyboard layout for the target script characters.
 IME pack only allows entry of runtime payload if there is a real backend in the firmware.

The Settings page selects `Locale Pack` instead of directly selecting fonts or IME.

## Built-in vs. external

The firmware intentionally carries only the minimum built-in baseline:

-Built-in locale pack: `en`
- Built-in font pack: `builtin-latin-ui`
- Built-in text candidate insertion capabilities: `Symbols` and selected `Emoji`
- Built-in text candidate content font baseline: `builtin-symbol-core` and `builtin-emoji-core`

So English, basic special symbol input and selected emoji input are always available even if no external language pack is present.

Runtime payloads can be discovered from external pack root directories, such as SD, or the Flash installation storage on the device.
The specific runtime root directory is defined by the current firmware implementation; the installed layout remains:

```text
/trailmate/packs/fonts/<font-pack-id>/manifest.ini
/trailmate/packs/locales/<locale-id>/manifest.ini
/trailmate/packs/ime/<ime-pack-id>/manifest.ini
```

On startup, the registry is built in two phases:

1. Cataloging all built-in packs and all external pack manifests.
2. Parse dependencies and determine which locales are allowed to be used based on the current memory profile.

Important note: The cost of discovering manifests is very low. The cataloging phase does not load external fonts into RAM.

## Three layouts

The same localized resource now exists in three representations at the same time. Deliberately separating them is part of the design requirements:

### Warehouse source code Bundle

Contents under `packs/<bundle-id>/` in Git.

- Contains runtime manifest
- Contains human-facing package metadata
- Contains files used only during build time, such as `charset.txt` and `build.ini`
- Might not track `font.bin` as Pages build can regenerate it

### CJK punctuation baseline

CJK font pack must treat commonly used Chinese/full-width punctuation as pack resources, rather than filling in bypasses at the firmware, renderer or message protocol layer.

The rules are as follows:

- There is only one shared punctuation source: `packs/common/cjk-punctuation.txt`.
- The `build.ini` of the CJK main font pack must incorporate this resource through `extra_chars_file=packs/common/cjk-punctuation.txt`.
- CJK main font packs that already have a complete `charset.txt` must also set `seed_charset_file=charset.txt` to prevent the original font set from being reduced to "translated text + punctuation" just by adding extra chars.
- `charset.txt` and `ranges.txt` must both cover all codepoints in the shared punctuation source.
- `tools/validate_locale_packs.py` is a pre-release guardrail for this rule and will be run by both CI and Pages builds.

This rule only defines the font resource coverage, does not change the text content, message protocol, BLE/MQTT behavior, and does not allow the UI layer to use character replacement to cover up missing words in pack.

### Installed runtime layout

The installed runtime layout that the firmware actually scans:

```text
/trailmate/packs/fonts/<font-pack-id>/...
/trailmate/packs/locales/<locale-id>/...
/trailmate/packs/ime/<ime-pack-id>/...
```

This is the only layout that the runtime registry understands.

### Distribution package

Objects that should be distributed and consumed by the website and future Extensions pages:

- One zip for each installable bundle
- A package manifest with version and compatibility metadata
- A description file for UI display
- A remote catalog entry for discovery and update checking

Zips will not be scanned directly at runtime. The package manager layer is responsible for downloading the zip, unpacking the payload into the installed runtime layout, and then requiring the runtime registry to be refreshed.

## Installed is not equal to Loaded

This is the central distinction of the entire design.

- `Installed`
 The manifest exists on the SD, and the registry knows that this pack exists.
- `Loaded`
 The external `font.bin` has actually been passed to `lv_binfont_create()` and now starts consuming runtime RAM.

The runtime behavior is as follows:

1. Resolve the current active locale from `settings/display_locale`.
2. Load the active UI font pack immediately when this locale is activated.
3. The active content font pack uses owner-controlled lazy loading and only requests loading when the content-scope text is actually needed.
4. On ESP, if the active locale explicitly declares `preferred_content_supplement_packs`, the registry can preload these cataloged content supplements according to the supplement budget during the locale activation phase.
5. If the current text contains codepoints that have not been covered by the active content chain, `FontRuntimeCoordinator` / `ResourcePackRegistry` arranges foreground loading, delayed retries or failure diagnosis of additional content supplement packs; pages/widgets must not read fonts privately, nor must they permanently skip installed available fonts.
6. When switching locale, all external fonts loaded at runtime will be unloaded and the entire chain will be rebuilt from scratch.

This means that a device can have many packs installed, but only the UI/content fonts required by the active locale and a small amount of content supplement allowed by the current memory profile will actually reside in RAM at any time.

## UI Scope and Content Scope

Intentionally maintain two different fallback chains when running.

### UI Chain

For static application chrome:

-Menu tab
-Settings tab
- Title
- Button
- Other translated interface text

 Links are as follows:

```text
screen-selected Latin base font -> active UI font pack
```

### Content Chain

 For user-generated or externally received text:

- Chat sender line
- Chat preview and text
- Contact name
- Team member name
- Node name and description
- Locale name displayed in the selector

 Links are as follows:

```text
screen-selected Latin base font
-> active content font pack
-> active UI font pack
-> registry-preloaded or lazily loaded content supplement packs
```

This is why under non-Chinese UI, as long as the corresponding pack is installed, the system can still display Chinese content correctly.

## Memory Profile

The availability of packs is governed by the board-specific memory profile in the shared runtime code.

The current profile is as follows:

- `constrained`
 The locale font budget is `128 KiB`, content supplement is disabled, the decoded map cache is `2` tiles, and the cache is not retained after the page exits.
- `standard`
 The locale font budget is `768 KiB`, the content supplement budget is `640 KiB`, and the maximum is `2` supplement pack. The decoded map cache is `12` tiles, and the cache is not retained after the page exits.
- `extended`
 The locale font budget is `2 MiB`, the content supplement budget is `2 MiB`, and the maximum is `3` supplement pack. The decoded map cache is `12` tiles, and the cache is retained after the page exits.

Current board-level mapping:

- `extended`:`Tab5`、`T-Display P4`
- `standard`:`T-Deck`、`T-Deck Pro`
- `constrained`: all remaining devices, including no PSRAM and pager-level targets

The locale budget is checked against the true cost of the currently active locale:

```text
unique(UI font pack, content font pack)
```

The supplement budget is separate from this and only applies to subsequent content packs pulled in for mixed script content.

## Persistence

Activity locale is stored as:

- namespace:`settings`
- key:`display_locale`

The integer `display_language` key used by the old version of ESP installation will be migrated to the new string key at once. After the migration is complete, the old key will be removed.

## Manifest Schema

Manifest uses the normal `key=value` file format.

### Font Pack Manifest

```ini
kind=font
id=zh-hans-core
display_name=Simplified Chinese Core
usage=both
estimated_ram_bytes=217456
source=binfont
file=font.bin
ranges=ranges.txt
```

Field description:

- `id`
 Stable pack identifier.
- `display_name`
 Human-readable name used for diagnostics.
- `usage`
 The value is `ui`, `content` or `both`.
- `estimated_ram_bytes`
 Expected runtime RAM cost after loading with LVGL binfont loader. This value is used for profile decisions and supplement planning.
- `source`
 Currently external files use `binfont`, and font aliases compiled into firmware use `builtin`.
- `file`
 The relative path of `font.bin`, relative to the pack directory.
- `ranges`
 Relative path to the override metadata file, relative to the pack directory. It is used for codepoint planning, not directly for rendering.

If `estimated_ram_bytes` is missing or `0`, the runtime will regard the pack as "unknown cost", so the budget cannot be accurately made. Pack in the repository should always provide this field.

### IME Pack Manifest

```ini
kind=ime
id=zh-hans-pinyin
display_name=Pinyin
backend=builtin-pinyin
```

Direct keyboard layout uses the same type of IME manifest, but the backend and layout must be declared separately:

```ini
kind=ime
id=ru-cyrillic-keyboard
display_name=Russian Cyrillic Keyboard
backend=builtin-keyboard-layout
layout=ru-cyrillic
```

The currently enabled input method implementations are:

- `zh-hans-pinyin`, the backend is `builtin-pinyin`
- `ru-cyrillic-keyboard`, the backend is `builtin-keyboard-layout`, the layout is `ru-cyrillic`

backend The real implementation of is located in the firmware code. The IME pack manifest is the layer that registers it into the runtime and exposes it.
`layout` is the target layout name of the direct keyboard layout backend; conversion backends such as Pinyin do not need to declare `layout`.
`builtin-keyboard-layout` must point to a keyboard layout descriptor known to the firmware through `layout`. The descriptor owns the touch keyboard map, key label font probe and mode label; the IME pack only references the descriptor and does not copy these display resources.

Hard rules:

- Only IMEs that already have a real input engine and a correct candidate vocabulary can appear in the runtime payload.
- Symbol/Emoji are not allowed to be published as IME packs. They are handled by the firmware's built-in `TextCandidatePicker`, and the entrance is the `Sym` / `Emoji` button in the text toolbar that is parallel to the IME switch button.
- The IME mode button is only responsible for switching `EN` / `IM` / `123`; it must not open the Symbol / Emoji candidate page.
- Direct keyboard layout must not write specific character table, font probe or layout id judgment into `ImeWidget`. Only the keyboard layout descriptor registry can be extended when adding a new layout.
- Unimplemented Kana, Korean, Arabic, Traditional Chinese Phonetic/Cangjie and Pan-Latin soft keyboards must not be occupied in advance with `backend=builtin-*`.
 - Only `ru-cyrillic-keyboard` is currently releasable for Cyrillic input; it is a direct keyboard layout, not a candidate word conversion engine.
- Taiwan Traditional Chinese Pinyin cannot be used by default. In the future, the preferred input method for `zh-Hant-TW` should be Zhuyin, and Cangjie/Cusheng can be used as an optional extension.
 - European Latin languages ​​generally do not require an IME pack; just use the normal `EN` / `123` input path.
- The locale manifest only writes `ime_pack` if a real IME is available. display-only locale does not write fake dependencies.

### Locale Pack Manifest

```ini
kind=locale
id=zh-Hans
display_name=Simplified Chinese
native_name=Simplified Chinese
translation_status=release
ui_font_pack=zh-hans-cjk
content_font_pack=zh-hans-cjk
ime_pack=zh-hans-pinyin
strings=strings.tsv
```

An example with layered Chinese coverage:

```ini
kind=locale
id=zh-Hans
display_name=Simplified Chinese
native_name=Simplified Chinese
translation_status=release
ui_font_pack=zh-hans-core
content_font_pack=zh-hans-core
preferred_content_supplement_packs=zh-hans-ext
ime_pack=zh-hans-pinyin
strings=strings.tsv
```

Field description:

- `id`
 Locale identifier used for persistence and logging. New locales should preferentially use BCP-47 forms that can express regional context, such as `zh-Hant-TW`, `pt-PT`.
- `display_name`
 Name for English context.
- `native_name`
 The name of this language shown in the selector.
- `ui_font_pack`
 This locale is used for the font pack of the interface chrome.
- `content_font_pack`
 This locale takes precedence over the font pack used on content surfaces.
- `preferred_content_supplement_packs`
 Optional comma-separated list of content supplement font packs; when this locale encounters missing words, try them first.
- `ime_pack`
 Optional IME dependency.
- `strings`
 Relative path to the locale TSV file, relative to the pack directory.
- `translation_status`
 Optional quality gate. When omitted, the package is temporarily considered releasable for compatibility with old packages; new packages must be explicitly written out. The values ​​are:
 - `release`: optional at runtime.
 - `review`: The string table structure is complete but has not passed the native language/region review and will be skipped at runtime.
 - `draft`: not completed or may be mixed with the wrong language, skipped at runtime.
 - Other unknown values: treated as unpublishable at runtime and skipped.

If `content_font_pack` is omitted, the default fallback is `ui_font_pack`.
If `ui_font_pack` is omitted, the default fallback is `builtin-latin-ui`.

## String Table format

Locale strings are stored in TSV:

```text
English source string<TAB>Localized string
```

Supported escapes include:

- `\\n`
- `\\t`
- `\\r`
- `\\\\`

Example:

```text
Settings	Paramètres
Send this code and compare:\\n	Envoyez ce code et comparez:\\n
```

The stable lookup key in code is always the English source string.

## String Table quality requirements

Release-level `strings.tsv` must meet:

1. It corresponds to the current benchmark key set one-to-one, and there is no shortage of keys.
2. There are no duplicate keys.
3. There is no empty translation.
4. Placeholders and escape characters such as `%s`, `%u`, `%ld`, `%.3f`, `\\n`, `\\t` must be consistent with the English key.
5. It is not allowed to copy the English key to the translation column as compensation.
6. Sentences in other languages ​​are not allowed.
7. Region-sensitive language must conform to regional word usage habits.

Only explicit proper names, protocol names, units, measurement labels or format skeletons are allowed to be retained in English, such as `GPS`, `HDOP`, `RSSI`, `dBm`, `RNode`, `LXMF`, `ID: !%08lX`.

The `review` package should also try to meet the above structural verification, but it may not enter the runtime language list temporarily because it requires native language review.

## Failure behavior

If a locale pack cannot be used:

- Missing dependent font pack: skip this locale
- Missing dependent IME pack: skip this locale
- `translation_status` is not `release`: skip this locale
 - The font cost of the active locale exceeds the current memory profile: skipping this locale
 - The persistent locale id can no longer be resolved: the runtime fallback to `en`

Does not erase the persistent `display_locale` value simply because a removable pack is currently absent. If the pack reappears later, the locale will become optional again.

If a content supplement cannot be loaded, the UI must still continue to survive. Only that specific text may appear missing, but the active locale will not change.

## Warehouse Bundle

The source code bundle currently provided by the warehouse is located at:

- `packs/ar`
- `packs/europe-cyrillic-ext`
- `packs/europe-latin-ext`
- `packs/zh-Hans`
- `packs/zh-Hant`
- `packs/ja`
- `packs/ko`

Each source bundle contains:

- `package.ini`
- `DESCRIPTION.txt`
- Build instructions in `README.md`
- Runtime payload tree under `fonts/`, `locales/` and optionally `ime/`
- `build.ini` and `build.ini` in each external font-pack directory `charset.txt`
 - `ranges.txt`

`font.bin` for use by runtime coverage planning is intentionally not part of the source-of-truth layout. If it already exists locally, it will be used directly; if it is missing, the Pages pack build will regenerate it based on the `charset.txt` and `build.ini` metadata in the bundle before generating the zip.

## Package Manifest

Each source bundle now has a bundle-level `package.ini`.

Example:

```ini
kind=package
id=zh-Hans
package_type=locale-bundle
version=1.0.0
display_name=Simplified Chinese
summary=Full Simplified Chinese locale bundle with external CJK font coverage and the built-in Pinyin IME backend.
description=DESCRIPTION.txt
readme=README.md
author=Trail Mate
homepage=https://vicliu624.github.io/trail-mate/
min_firmware_version=0.1.18-alpha
supported_memory_profiles=standard,extended
tags=language,cjk,chinese,ime
```

Field description:

- `id`
 Stable package identifier for use by the remote catalog and future installed-index files.
- `package_type`
 High-level package role. Legal values ​​include:
 - `locale-bundle`: Provide at least one locale, possibly also a font/IME.
 - `content-bundle`: Provides a content font, supplement, or content-related IME, but does not declare a locale.
 - `input-bundle`: mainly provides IME runtime payload, optionally relying on existing fonts.
 Historical null values ​​are handled compatible with `locale-bundle`.
- `version`
 Package version number, independent of firmware tag. User-visible changes to strings, manifests, fonts, quality status, or IME dependencies must be bumped.
- `display_name`
 The package name shown to the user in the Extensions UI.
- `summary`
 A one-line description for the list view.
- `description`
 A plain text description file relative to the bundle root directory.
- `readme`
 Developer-oriented documentation files relative to the bundle root directory.
- `author`
 package publisher string.
- `homepage`
 Project or package homepage.
- `min_firmware_version`
 Correctly understand the minimum firmware version required by the bundle contract. Add manifest semantics that must be understood at runtime. For example, when non-release `translation_status` skips rules, the lower bound must be raised.
- `supported_memory_profiles`
 Declarative compatibility hint, can be `constrained`, `standard` and/or `extended`.
- `tags`
 Metadata for future browser search and grouping of Extensions.

Version bump convention:

-Fix a small number of missed translations or typos: patch.
- Complete translations, adjust regional terms, change quality status, remove fake IME: minor.
 - Changing payload layout or manifest semantics misinterpreted by older firmware: Raise `min_firmware_version` and impact bump minor/major.

## Font construction metadata

Each external font-pack directory may also declare a `build.ini` for toolchain use only.

Example:

```ini
font=tools/fonts/NotoSansCJKsc-Regular.otf
size=16
bpp=2
no_compress=true
```

Field description:

- `font`
 Source font files in the warehouse.
- `size`
 The pixel size passed to the binfont generator.
- `bpp`
 Generate fonts bits-per-pixel.
- `no_compress`
 Whether to have the generated `font.bin` disable RLE compression of lv_font_conv.

This file only belongs to the metadata during the build period. It is never read at runtime.

## Distributing Archive layouts

GitHub Pages builds now generate a zip for each bundle at the following path:

```text
site/assets/packs/<package-id>-<version>.zip
```

Each archive contains:

```text
package.ini
DESCRIPTION.txt
README.md
payload/fonts/<font-pack-id>/...
payload/locales/<locale-id>/...
payload/ime/<ime-pack-id>/...
```

`payload/` contains only installable runtime files. Build-only files like `charset.txt`, `build.ini` and `.gitignore` will be excluded from the archive.

## Remote Catalog

Pages build also generates:

```text
site/data/packs.json
```

This catalog is the discovery surface of the future Extensions UI. Each entry contains:

- package id, version, display name, summary and long description
- Compatibility tips such as `min_firmware_version`, supported memory profiles
- List of locale/font/IME records provided by the bundle
- Archive path, size and SHA-256 for download and update checking
- Estimated total runtime font RAM for pre-installation planning

This catalog is intentionally designed to be on top of the runtime registry. It describes a "downloadable bundle", not a "currently loaded font".

## Installed Package Index

The runtime registry still catalogs unpacked resources directly from `/trailmate/packs`.
For package management, the corresponding installer layer should be additionally maintained:

```text
/trailmate/packs/.index/installed.json
```

This file should record:

-Installed package id
-Installed package version
-Installation time
-SHA-256 of the source archive

Future update prompts should combine this installed index with `site/data/packs.json` Compare instead of guessing based on directory names.

## Current IME coverage

Currently, only real locale IMEs are allowed to be declared in the warehouse bundle:

1. A locale bundle can declare an IME dedicated to this locale.
2. The input bundle can declare a real input engine added in the future, but it must already have firmware backend support.

Specific constraints:

- European Latin locales should not hang false `builtin-latin` in order to "have input method list items".
- Symbol/Emoji is the built-in text candidate insertion capability of the firmware and does not belong to any locale or pack.
- `builtin-symbol-core` / `builtin-emoji-core` only cover the 100 Symbol / 100 Emoji candidates built into the firmware, which belong to the content font baseline and do not occupy the external content supplement quantity budget.
- `symbol-picker`, `emoji-picker`, `builtin-candidate-picker` and `candidates.txt` are old candidate list IME models and must not be released as new runtime payloads.

## New content/input extensions

Content or input extensions can have no locale, but must still comply with the runtime payload layering:

1. The source bundle is located in `packs/<bundle-id>/`.
2. `package.ini` uses `package_type=content-bundle` or `package_type=input-bundle`.
3. The font payload is still placed in `fonts/<font-pack-id>/`.
4. IME payload is still placed in `ime/<ime-pack-id>/`.
5. `README.md` must explain that it is not a locale and how the user can enable it.
6. `supported_memory_profiles` must be declared by font RAM and supplement policy.
7. If it requires a new runtime IME backend, `min_firmware_version` must be increased.

The emoji extension reference implementation is currently no longer provided. Selected Symbol / Emoji candidates with `builtin-symbol-core` / `builtin-emoji-core` fonts are built in by the firmware, the number is capped by the built-in text candidate insertion surface of `LOCALIZATION_SPEC.md`.

## Add a new language

1. First decide whether this locale can reuse an existing font pack, or whether it needs to create a new one.
2. Decide whether the locale id requires a region suffix, such as `zh-Hant-TW`, `pt-PT`.
3. Add source code bundle: `packs/<bundle-id>/`, and write `package.ini`, `DESCRIPTION.txt` and `README.md`.
4. Generate `charset.txt` and `ranges.txt` for each external font pack in the bundle.
5. Write `build.ini` for each generated font pack.
6. Write runtime font manifest, including `usage`, `estimated_ram_bytes` and `ranges`.
7. Write locale pack manifest, including `ui_font_pack`, `content_font_pack`, `translation_status`.
8. The initial language package should use `translation_status=review` or `draft` first, and then it can be changed to `release` after the native language/region review is completed.
9. Add `ime_pack` only if the real input engine has been implemented.
10. Verification of `strings.tsv`: missing keys, duplicate keys, empty translations, inconsistent placeholders, and English compensation items.
11. Bump `version` in `package.ini` and increase `min_firmware_version` if necessary.
12. Run `python scripts/build_pack_repository.py --pack-root packs --site-root site`.
13. You can manually copy the runtime payload to SD for installation, or provide it to future Extensions installers through the zip generated by Pages.
14. Only release locale will be selectable in Settings.

## Design rules

This architecture intentionally avoids the following practices:

- Use integer language enums
- Eagerly load all installed fonts on boot
- Use a global "switch fonts only if non-ASCII" shortcut rule
- Mix UI chrome text with user content text
- Let localization implementations on different platforms gradually drift apart
-Confuse runtime manifest and package distribution metadata
- Let the runtime registry understand the remote catalog or zip archive
- Disguise the release package with machine translation, traditional-simplified conversion, or English compensation
- Release a fake IME when there is no real input method backend

Keep the dependency graph explicit:

```text
Locale Pack -> UI Font Pack
Locale Pack -> Content Font Pack
Locale Pack -> IME Pack
Content text -> Optional supplement font packs
```

This not only makes the code easier to reason about, but also allows Trail Mate to avoid forcing all devices to bear the same firmware size. There are paths to expand to wider language support at lower RAM costs.
