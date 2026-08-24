# Locale Pack Release Specification

This document defines the packaging, publishing, updating and rollback rules for the Trail Mate language/content/input extension package.

The question it answers is not "how to translate a sentence", but:

1. What changes must bump package version.
2. What changes must increase `min_firmware_version`.
3. How release/review/draft packages enter archive and catalog.
4. How users can see language pack updates.
5. What structural verification must be run before publishing.

If this document conflicts with `LOCALIZATION_SPEC.md`, `LOCALIZATION_SPEC.md` shall prevail.
If this document conflicts with `LOCALE_PACKS.md`, the release process of this document shall prevail.

---

## 1. Publishing objects

Language/content/input extension package publishing involves five objects:

1. source bundle
 - `packs/<bundle-id>/` in the warehouse.
2. runtime payload
 - `payload/fonts`, `payload/locales`, `payload/ime` in archive.
3. package manifest
 - `package.ini` in the bundle root directory.
4. distribution archive
   - `site/assets/packs/<package-id>-<version>.zip`.
5. remote catalog
   - `site/data/packs.json`.

These five objects must evolve together. Only changing the source bundle without rebuilding the archive/catalog does not constitute a complete release.

---

## 2. Package status

The `translation_status` of the locale manifest controls whether the locale can be used at runtime.
`content-bundle` / `input-bundle` without locale does not participate in `translation_status`
 judgment, but still must pass font/IME payload verification.

1. `release`
 - Can appear in the language selector.
 - Must pass structural verification and quality review.
2. `review`
 - Can be packaged, distributed, installed, and updated.
 - Must be skipped at runtime and does not enter the language selector.
 - Used to get the complete payload to testers or native language reviewers.
3. `draft`
 - Missing keys or incomplete translations are allowed.
 - Must be skipped at runtime.
 - In principle, it should not enter the public catalog unless the catalog is explicitly marked as an experimental package.

New language packs must not omit `translation_status`. Omitted only for compatibility with historical packages.

---

## 3. Version Bump Rules

The `version` of `package.ini` is one of the main signals for users to see updates. The following changes must bump version:

1. `strings.tsv` changes.
2. locale/font/IME `manifest.ini` changes.
3. `font.bin`, `ranges.txt`, `charset.txt` changes.
4. Description changes in `README.md` and `DESCRIPTION.txt` that affect user understanding.
5. `translation_status` changes.
6. `ime_pack` additions and deletions or IME backend changes.
7. Field changes in `package.ini` that affect installation, compatibility, search, and display.

Suggested bump range:

1. patch
 - A few missing translations, typos, punctuation, and terminology corrections.
 - For example `1.0.0 -> 1.0.1`.
2. minor
 - A large number of complete translations.
 - Enter `review` from `draft` or enter `release` from `review`.
 - Remove fake IME or change input method policy.
 - Added a large number of key overrides.
 - For example `1.0.0 -> 1.1.0`.
3. major
 - Destructive changes in payload layout, dependency semantics, or compatibility semantics.
 - Major can be used sparingly in the current pre-1.0 stage, but patches must not be used to cover up destructive changes.

It is forbidden to change the archive content but keep the same package version.

---

## 4. `min_firmware_version` rule

`min_firmware_version` represents the minimum firmware version required to correctly understand the bundle contract.

The following changes must increase `min_firmware_version`:

1. Add manifest fields that must be understood at runtime.
2. Change the semantics of existing manifest fields.
3. Change locale/font/IME dependency rules.
4. Change the runtime payload layout.
5. Installing this package on older firmware may expose incorrect languages, fake IMEs, or incorrect font chains.

Currently `0.1.24-alpha` introduces `translation_status=review/draft` and requires the runtime to skip non-release locales, so all review/draft packages that rely on this quality gate must increase `min_firmware_version` to `0.1.24-alpha` or higher.

---

## 5. Archive rules

The archive path is fixed to:

```text
site/assets/packs/<package-id>-<version>.zip
```

archive must contain:

```text
package.ini
DESCRIPTION.txt
README.md
payload/fonts/<font-pack-id>/manifest.ini
payload/fonts/<font-pack-id>/ranges.txt
payload/fonts/<font-pack-id>/font.bin
payload/locales/<locale-id>/manifest.ini
payload/locales/<locale-id>/strings.tsv
payload/ime/<ime-pack-id>/manifest.ini
```

Among them `payload/locales/...` and `payload/ime/...` are optional according to package type:

1. `locale-bundle` must provide at least one locale.
2. `content-bundle` does not need to provide a locale, but must provide at least one font or IME.
3. `input-bundle` does not need to provide a locale, but it must provide at least one real runnable IME.

`payload/ime/...` is only allowed to contain real runnable IMEs. Unimplemented input methods are not allowed to enter the archive.

archive must not contain:

1. `build.ini`
2. `charset.txt`
3. `.gitignore`
4. Temporary files
5. Empty IME directory
6. Unused experimental payload

---

## 6. Catalog rules

`site/data/packs.json` is the catalog where users see installable/updatable expansion packages.
Extensions UI must combine `locale-bundle`, `content-bundle` and `input-bundle`
 are all considered installable extensions, rather than only showing language packs.

Every time the package version or archive content changes, the catalog must be rebuilt to synchronize the following fields:

1. package id
2. package version
3. display name
4. summary
5. `min_firmware_version`
6. supported memory profiles
7. provided locale/font/IME records
8. archive URL/path
9. archive size
10. archive SHA-256
11. estimated runtime font RAM

If the version or SHA-256 of the catalog does not change, the user-side update check may not see the change.

---

## 7. Structure verification before publishing

Each locale `strings.tsv` must be verified:

1. The key set is complete.
2. No duplicate keys.
3. No time for translation.
4. The printf placeholders are consistent.
5. The escape characters are consistent.
6. There is no English compensation item in the release package.
7. There is no third language mixed into the release package.

Each locale manifest must verify:

1. `id` exists.
2. `translation_status` exists.
3. `ui_font_pack` exists and can be parsed.
4. `content_font_pack` exists and can be parsed.
5. `ime_pack` only points to the real runtime IME.
6. RTL locale must declare `direction=rtl`.

Each font/IME-only package must verify:

1. The package type is not empty and is not a disguised `locale-bundle`.
2. Provide at least one font or IME record when there is no locale.
3. IME backend is supported by the current firmware runtime; the candidate list backend is no longer a legal pack backend.
4. font `usage` matches the package semantics; content supplement must use `usage=content`.
5. If the IME requires a layout table, these data must be published as a pack payload file and cannot be completed by the firmware by package id. Symbol/Emoji candidates belong to the firmware built-in `TextCandidatePicker`, not the pack payload, and cannot be published through `candidates.txt`.

Each package manifest must be verified:

1. `version` has been bumped.
2. `min_firmware_version` matches manifest semantics.
3. `supported_memory_profiles` matches font RAM cost.
4. `summary` does not lie about existing unimplemented IME.

---

## 8. Publishing process

Standard process:

1. Modify the source bundle.
2. Run locale structure verification.
3. Determine the `translation_status` of each locale.
4. Remove all false dependencies that do not implement IME manifest and locale `ime_pack`.
5. bump the `version` of `package.ini`.
6. Increase `min_firmware_version` if necessary.
7. Rebuild the archive and catalog:

```powershell
python scripts/build_pack_repository.py --pack-root packs --site-root site
```

8. Verify that the version, archive path, size, and SHA-256 in `site/data/packs.json` have all undergone expected changes.
9. Verify on target firmware:
 - release locale optional.
 - review/draft locale does not appear in the language selector.
 - The IME list in Settings only shows real available IMEs.
 - Update available can be seen when old packages are installed.

---

## 9. Rollback rules

If serious problems are found in published language packs:

1. Do not reuse old archive file names to overwrite content.
2. Release a higher version of the fix package.
3. If it must be removed from the shelves, remove the package record from the catalog and keep the release note.
4. If the release quality is misjudged, a higher version should be released and the locale should be lowered back to `review` or `draft`.

In this way, installed index and update check can explain "why users need to update".

---

## 10. Current release strategy

The current strategy is:

1. `zh-Hans`
 - Available as `release`.
 - The publishable IME is `zh-hans-pinyin`.
2. `zh-Hant`
 - Processed according to Taiwan Traditional Chinese `review` package.
 - Do not hang Pinyin IME.
 - Before entering `release`, you must go through Taiwanese terminology and input method custom review.
3. `ru`
 - Can be handled as a `review` package.
 - Publishable IME is `ru-cyrillic-keyboard`, backend is `builtin-keyboard-layout`, layout is `ru-cyrillic`.
 - This IME is a direct virtual keyboard layout and does not commit to physical keyboard hardware remapping.
4. `ar`、`de`、`es`、`fr`、`it`、`nl`、`pl`、`pt-PT`、`ja`、`ko`
 - Can be packaged as `review`.
 - Does not enter the normal user language selector.
 - Do not publish fake IMEs.
