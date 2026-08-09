# Packet Probe Pager UI

## Visual Language

Packet Probe follows the current Map page language rather than the former
Energy Sweep dashboard language.

| Token | Value | Use |
| --- | --- | --- |
| Work surface | `#FFF3DF` | Full screen beneath the top bar |
| Keycap surface | `#F8E6C3` | Bottom-bar controls and result rows |
| Divider | `#B3915D` | Result-row and keycap borders |
| Primary text | `#593D1C` | Labels and frequency values |
| Secondary text | `#846A42` | Parameter details and progress |
| Amber | `#EBA341` | Selected result and Apply action |
| Blue | `#315F91` | Listening state and selected frequency |
| Green | `#397046` | Evidence count and applied confirmation |

No page-level floating cards are used. Result rows are interactive repeated
items; the rest of the screen remains a continuous work surface.

## 480x222 Layout

```
0,0     +------------------------------------------------+
        | <  PACKET PROBE                           battery |
30      +------------------------------------------------+
        | OBSERVED PARAMETERS                                 |
        | LISTENING  12/70 PASS 1                             |
        | [433.550 MHz      BW250 SF11 CR4/5            x12] |
        | [433.775 MHz      BW250 SF11 CR4/5            x03] |
        | 12/70 PROFILES  2 OBSERVED                         |
198     +------------------------------------------------+
        | UP/DN SELECT | ENTER SET | S STOP | ESC BACK   |
222     +------------------------------------------------+
```

- Top bar: 30 px, supplied by the shared top-bar component.
- Work area: `y=30..198`, 10 px outer margin, one full-width list surface.
  There is no selected-profile detail column, so the same layout remains
  readable on T-Deck-class narrow displays.
- Bottom bar: 24 px, using the Map control-bar keycap treatment.
- Pager result rows: 24 px high, 3 px gap, up to four visible rows. `UP/DN`
  automatically moves the four-row window when the selection crosses a page
  boundary, so every observed profile remains visible and selectable.

## Content Rules

The full-width result list shows only real observations:

- The main line is the center frequency.
- The secondary line is bandwidth, SF, and CR.
- The right-aligned `xN` value is the number of evidence packets.

The selected row is the only selection-state presentation. It is amber and
keeps the same frequency, air parameters, and right-aligned evidence count as
every other row. The page does not repeat selected details, RSSI metadata, or
business-packet labels elsewhere. After an apply, the status line changes to
`APPLIED TO MESH`.

Empty state text is factual: `NO OBSERVED PROFILES` before scanning and `NO
VALIDATED PACKETS` while a pass is in progress. It does not claim the candidate
space is empty.

## Keyboard And Touch Interaction

| Input | Action |
| --- | --- |
| `UP` / `DOWN` | Select an observed profile |
| `ENTER` | Open the apply confirmation for the selected profile |
| `S` | Start or stop the probe |
| `ESC` / Back | Close the dialog or leave the page |
| Tap/click a result row | Select that profile |
| Tap/click bottom `SET` or `START/STOP` | Same action as the matching key |

The bottom bar reflects the active state by displaying `S START` before a
probe starts and `S STOP` while it owns the radio.

## Confirmation Overlay

Applying a result is intentionally not a one-key operation.

```
                  +----------------------------+
                  | APPLY OBSERVED PROFILE?    |
                  | 433.550 MHz                |
                  | BW 250k  SF11  CR4/5       |
                  | [ESC CANCEL] [ENTER APPLY] |
                  +----------------------------+
```

The full-screen scrim is warm dark brown at 50 percent opacity. The modal is
warm white with a 2 px amber border and 8 px radius, matching the existing
tracker confirmation treatment. `Cancel` owns initial focus. Arrow left/right
moves between the two actions; Enter applies only after Apply is focused.
