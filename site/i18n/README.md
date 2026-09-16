# Website localization

The website supports English, Simplified Chinese, Taiwan Traditional Chinese,
Japanese, Korean, Arabic, Russian, German, Spanish, French and Italian.

Website language, firmware interface language and downloadable device language
packs are separate concepts. Selecting a website language does not install a pack
or change text rendered by the firmware inside the simulator.

## Directory contract

- `registry.js`: the supported IDs, native language names and writing direction.
  Both the page and component language selectors use this registry.
- `<locale>.js`: complete page, status, accessibility and package-description
  catalogs. Every language has the same semantic keys and parameter names.
- `features-<locale>.js`: independent names, summaries, descriptions and hardware
  requirements for each feature, including English. Stable IDs and source links
  remain in `../explorer-data.js`.
- `runtime.js`: lazy loading, active locale, aliases, interpolation and change
  notifications. Missing translations throw instead of silently showing English.
  The last requested language wins during asynchronous loading.
- `dom.js`: translates `data-i18n`, `data-i18n-aria`, `data-i18n-alt` and
  `data-i18n-placeholder`. Catalogs contain plain text, never executable markup.
- `component-labels.js`: connects the compiled React bundle to the same external
  runtime instance used by the page. Do not bundle a second language store.
- `pack-presentation.js`: formats device-pack names, descriptions, statuses,
  counts and memory profiles. `Intl.DisplayNames` localizes standardized language
  names, and `Intl.NumberFormat` formats counts. Prose is manually authored in
  the catalogs; English release metadata is not used as a translation fallback.
- `check-catalogs.mjs`: validates catalog keys, values and interpolation markers.

`../main.js` stores the choice in local storage and updates the URL's `lang`
parameter. Explicit URL selection takes precedence on initial load. The runtime
sets the document's `lang` and `dir`; Arabic uses RTL while SVG device geometry
remains LTR. Arabic formulas use Unicode directional isolation.

## Adding or revising a language

1. Add its BCP 47 ID, native name and direction to `registry.js`.
2. Create `<locale>.js` with all keys from `en.js`, and
   `features-<locale>.js` with all feature IDs and fields from `features-en.js`.
3. Translate complete sentences in natural regional usage. Preserve product
   names, URLs, hardware IDs and printed device legends. Taiwan Traditional
   Chinese requires its own terminology, not automatic character conversion.
4. Preserve placeholders such as `{tag}` and `{count}`. Use complete count
   labels instead of joining English singular/plural suffixes to a number.
5. New published device packs need a translated description in every page catalog
   and an entry in `describedPacks` in `pack-presentation.js`. Preserve input,
   review and hardware limitations; do not imply an unavailable capability.
6. Rebuild components after changing the registry or component source, and run
   the checks below.
7. Check the actual dropdown, a reload, feature search, package cards, errors and
   long text in the browser. For RTL, check mixed-script text and switch back to
   an LTR language. Automated completeness tests do not judge translation quality.

Do not use translation APIs or character-conversion tools as a substitute for
writing and reviewing the translations.

## Checks

From the repository root:

```sh
node site/i18n/check-catalogs.mjs
node --test tests/site/i18n-*.test.mjs
```

Tests cover all registered catalogs, actual lazy loading, feature coverage,
parameter parity, document direction, rapid switching, translated markup
attributes and the published device-pack catalog. The Pages workflow runs these
checks before publishing.

Browser verification of the current change covered all eleven selections at a
1280 px viewport, localized pack content and feature search, RTL-to-LTR switching,
URL persistence, and absence of page-level horizontal overflow. SVG geometry is
also checked at five available widths by `device-geometry.test.mjs`.
