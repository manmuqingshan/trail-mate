# Trail Mate GitHub Pages

The site is a static product guide. Its language is independent of the firmware
language shown inside the WebAssembly preview and of downloadable device packs.

## Responsibilities

| Path | Responsibility |
| --- | --- |
| `index.html` | Semantic sections and translation markers; product links |
| `main.js` | Release/pack data loading, install panel and page startup |
| `i18n/` | Website catalogs, locale state, DOM binding and localized pack presentation |
| `components.jsx` | Animal Island UI Select, Title, Icon, Background and BackTop |
| `components-dist/` | Browser-ready component bundle and font assets |
| `explorer-data.js` | Stable feature IDs, icons and documentation links; no translated prose |
| `native-explorer.js` | Native preview lifecycle and browser input bridge |
| `simulator/devices/` | Manufacturer, display geometry, SVG artwork and physical controls |
| `simulator/components/` | SVG mounting, input overlay, fitting and control labels |
| `assets/device/` | Device artwork supplied for this project |
| `data/`, `assets/native/`, `assets/packs/` | Generated release, native-preview and pack artifacts |
| `../tools/web_simulator/` | C++ host and simulated hardware services for real LVGL pages |
| `../tests/site/` | Locale, geometry and native page checks |

## Local preview

```sh
cd site
npm ci --ignore-scripts
npm run build:components
cd ..
python -m http.server 8765 --directory site --bind 127.0.0.1
```

Open `http://127.0.0.1:8765/`. The simulator also needs generated native assets;
see `../tools/web_simulator/README.md` for the Emscripten build.
Use HTTP for local development so module and data requests behave like Pages.
All asset URLs are relative and support the `/trail-mate/` deployment prefix.

## Product boundaries

- Manufacturer filtering defaults to all devices. Simulator manufacturer and
  device selectors are linked through the device registry.
- Pager, T-Deck and Wio Tracker L2 have native previews. T-Echo-Lite is a
  manual-install target. Wio Tracker L2 is not yet on sale; its purchase control
  is disabled. The SVG viewport fits available space automatically.
- Web Flasher renders one device/radio panel. Release metadata determines
  artifact availability; selecting another target does not download its firmware.
- Device legends and native framebuffer text belong to the device/firmware.
  Website language selection translates the surrounding guide and controls.
- Native pages share firmware C++ view code, but radio, GNSS, storage, chat data
  and power state are simulated. This is an interface preview, not a guarantee of
  complete hardware or protocol behavior.
- Meshtastic and MeshCore support serves basic outdoor communication; full
  protocol parity and unrelated advanced radio features are outside this claim.
- Survey examples use measurements entered by the user. They do not imply a
  built-in rangefinder or automatic angle measurement.

## Verification and publishing

```sh
node site/i18n/check-catalogs.mjs
node --test tests/site/*.test.mjs
```

The native test requires the generated JS/WASM/data artifacts for all three
preview profiles. The Pages workflow builds them, verifies website catalogs and
device packs, prepares release assets, then builds the component bundle.
Do not deploy `node_modules` or local build directories.

See `i18n/README.md` for adding or revising a language and `THIRD_PARTY.md` for
credits. Changes to website text must preserve hardware and protocol boundaries.
