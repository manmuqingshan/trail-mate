# Firmware Visual Style Specification

## 1. Scope

This document defines the overall visual language of the Trail Mate firmware interface.

What it constrains is:

-Color system
-Top chrome style
-Basic style of panel/button/pop-up window
-Text level and alignment principles
- The overall temperament that the page should present

It **not** defines:

- The pixel-level geometric layout of a certain page
- The fixed size of a certain device profile
- The internal implementation details of a certain component

This means:

- `480x222`
- `pager`
- `tdeck`
- Any specific `x / y / w / h`

 can only appear in "page specifications" or "device/profile examples" and cannot be promoted to a general layout rule for the entire firmware.

The pixel tables in `docs/skyplot.md`, `docs/sstv/SSTV.md` and `docs/EnergySweep/uiux.md` can only explain how their respective pages are implemented on their respective target profiles; they share a visual language, not the same set of geometry.

---

## 2. Distinctions

### 2.1 Visual style != Page layout

Visual style answers "what does this firmware look like".

Page layout answers "how a certain page is placed on a certain profile."

The former is a global constraint, and the latter is a page-level constraint.

### 2.2 Page layout != device profile

The same page can maintain the same visual language on different devices, but use different sizes, margins, font sizes and control densities.

Thus:

- Visual style should be stable across devices
- Pixel layout should follow `page_profile`, screen size and interaction mode adjustment

### 2.3 Page chrome != Page content

The following elements belong to shared chrome:

- TopBar
- Return to entry
- Power/status bit
- Fixed floating control button
- Basic style of pop-up window/bottom pop-up layer

The following elements belong to the page content:

- Map
- List
- Telemetry information
- Image area
- Chart/status panel

Chrome should maintain global consistency; content can change with the semantics of the page.

### 2.4 Semantic Color != Decorative Color

Color is first used to express hierarchy and semantics, not to create "fancy".

Acceptable color divisions are:

- Main emphasis: Amber
- Main text: Text
 - Subtext: TextDim
 - Message: Info
 - Success: Ok
 - Warning: Warn

It is unacceptable to randomly invent a set of style-independent colors for each line and each label.

---

## 3. Canonical Tokens

The baseline tokens for the overall style of the firmware interface are as follows:

- `Amber` = `#EBA341`
- `AmberDark` = `#C98118`
- `WarmBG` = `#F6E6C6`
- `PanelBG` = `#FAF0D8`
- `Line` = `#E7C98F`
- `Text` = `#6B4A1E`
- `TextDim` = `#8A6A3A`
- `Warn` = `#B94A2C`
- `Ok` = `#3E7D3E`
- `Info` = `#2D6FB6`

New and refactored pages should prioritize building a visual around this set of tokens.

If a shared theme/helper already exists in the implementation layer, the helper should be converged towards this set of tokens instead of continuing to emanate new dark private systems in the page.

---

## 4. Global Style Direction

Trail Mate's interface should be unified into a "warm engineering instrument style" rather than a dark cyber/HUD style.

Its temperament should be:

- Warmth
- Restraint
- Readable
- Engineering
- Lightweight instrumentation

It should not be:

- Black and blue neon HUD
- Debug panel stacking
- Highly saturated cyber style
- Heavy decoration UI with shadows and reliefs everywhere

---

## 5. Layout Principles

### 5.1 Root background

Use `WarmBG` first for the page root background.

The main carrying area where maps, pictures, lists or charts are located can be layered with `PanelBG` or a content-specific background color, but it must not deviate from the overall warm tone.

### 5.2 TopBar

All standard pages give priority to reusing the shared `top_bar` component.

The semantics of TopBar is "shared application chrome", not a new title bar invented by the page itself.

Requirements:

-The color should be consistent with the global theme
-Do not allow partial customization of the page into another set of title bar styles
-The title should be centered
-The status information on the right should remain weakly hierarchical

### 5.3 Panels and Borders

If the page really needs a container, it should comply with:

- Background priority `PanelBG`
- Border priority `Line` or `AmberDark`
- Uniform rounded corners 8~10px
- Border thickness is preferred 2px

It is not allowed to cram all information into multiple levels of nested cards.

### 5.4 Buttons

Buttons should follow the warm engineering style:

- Default state: `PanelBG` + `AmberDark/Line` border
- Focused state: `Amber` outline or highlight
- Disabled state: weaken the background and borders, but still remain recognizable

It is not allowed to introduce a dark, metallic blue, and glass button system into a single page.

### 5.5 Pop-up windows and pop-up layers

Pop-up windows should inherit the global style instead of becoming an island of style.

Requirements:

- Use a warm panel for the background
- Use `AmberDark` or `Line` for the border
- Prioritize the use of compact elastic layers/bottom elastic layers on small screens instead of huge centered black modal blocks
- The interactive entrance cannot be difficult to operate due to the obstruction of elastic layers

### 5.6 Text level

Text level should be stable:

- Primary object name/primary reading: `Text`
- Secondary description/status: `TextDim`
- Emphasis on information: `AmberDark` or `Amber`
- Status semantics: `Info / Ok / Warn`

Do not make the page into a color bar board with "all text in different colors" by default.

### 5.7 Right Telemetry Column

When the right side of the page assumes the responsibility of "telemetry/link/readout", this column should be treated as a "right edge reading column":

- Overall right alignment
- One item per line
- Field semantics are direct, short, and scannable
- Do not make it like "move left-aligned paragraphs to the right"

---

## 6. Geometry Rules

Global visual specifications only stipulate geometric principles, not unified pixels.

Allowed global principles:

- Reserve the shared TopBar area at the top
- Content area is freely allocated according to page semantics
- Margins, font size, and button size can be adjusted with `page_profile`

Unallowed misreading:

- Put a certain page in `480x222` on `pager` Layout, treat it as a layout that all pages or all devices must copy
- Treat the double-column layout of a certain page as a double-column layout that all pages must copy
- Treat the button coordinates of a certain page as the only legal position of a shared component

---

## 7. Guardrails

Subsequent page design and modification must comply with the following constraints:

1. It is not allowed to introduce dark HUD styles into standard content pages.
2. Promoting `pager` or any single-device layout example to a global geometry specification is not allowed.
3. It is not allowed to create a title bar with conflicting styles outside of the shared TopBar.
4. It is not allowed to use "left-aligned paragraph text" in the right telemetry column to pretend to be a reading column.
5. Pop-up windows are not allowed to be made into dark modal blocks that are fragmented, severely obscured, and difficult to operate.
6. It is not allowed to break through the boundaries of semantic colors and turn colors into random decorations for the sake of "looking good".

---

## 8. Summary Baseline

To summarize this specification in one sentence:

The firmware interface of Trail Mate should be unified into a "warm engineering instrument style", and the specific pixel layout is always a page-level and profile-level decision, and cannot be reversely legislated from a certain device example into a global rule.
