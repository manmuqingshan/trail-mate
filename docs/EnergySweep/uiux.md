# Protocol Probe Pager UI

## Product State

This is the target UI for Protocol Probe (Chinese: 协议包探测). It replaces the
former Energy Sweep / Spectrum dashboard. The internal route remains
`energy_sweep`; the visible name is `PROTOCOL PROBE` and the menu icon is
`radar.c`.

The page answers whether a complete protocol air profile has been observed or
confirmed. It never presents RSSI as proof, recommends a quiet channel, or
names a received business payload.

## Visual Language

Protocol Probe follows the current Map page language rather than the former
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
items; the rest of the screen remains a continuous work surface. There is no
selected-profile detail panel, so Pager and T-Deck use the same layout.

## 480x222 Layout

```
0,0     +------------------------------------------------+
        | <  PROTOCOL PROBE                         battery |
30      +------------------------------------------------+
        | MT 433.775 B125 S11 3/12                        |
        | [MT 433.775  125K SF11 C4/8       OBSERVED x04] |
        | [RT 433.775  125K SF09 C4/5       OBSERVED x18] |
        | [MC 433.550  250K SF07 C4/5      CONFIRMED x01] |
        | 3 FOUND   TX only during protocol verification   |
198     +------------------------------------------------+
        | UP/DN SELECT | ENTER SET | S STOP | ESC BACK   |
222     +------------------------------------------------+
```

- Top bar: 30 px, supplied by the shared top-bar component.
- Work area: `y=30..198`, 10 px outer margin, one full-width list surface.
- Bottom bar: 24 px, using the Map control-bar keycap treatment.
- Pager result rows: 24 px high, 3 px gap, up to four visible rows. `UP/DN`
  automatically moves the four-row window when the selection crosses a page
  boundary, so every observed profile remains visible and selectable.

The live phase line names the action actually in progress:

| Phase | Label | Meaning |
| --- | --- | --- |
| Passive candidate RX | `MT 433.775 B125 S11 3/12` | The radio listens on this complete candidate profile. |
| MeshCore request | `MC TX 433.775` | Discover was sent and the response window is open. |
| Meshtastic check | `MT ACK 433.775` | A targeted `want_ack` check is running. |
| Reticulum traffic | `RT TRAFFIC 18` | Valid Reticulum frames were seen on the profile. |

## Content Rules

Each row uses fixed-width content:

```text
PROTOCOL  FREQUENCY  BW/SF/CR  STATE  xEVIDENCE
```

`OBSERVED` means protocol-level evidence. `CONFIRMED` means a correlated
MeshCore Discover response or Meshtastic routing ACK. Reticulum remains
`OBSERVED` in Protocol Probe; a later normal-network Ping after applying its
profile is separate from probing. The evidence count totals protocol
frames/responses and never lists business payload types.

Rows with RF activity or only a generic LoRa frame are diagnostics, not
selectable result rows. A timeout never removes an existing observation.

The selected row is the only selection-state presentation. It is amber and
keeps the same fixed fields as every other row. Confirmed state uses green
treatment without changing layout. The page never repeats selected details,
RSSI metadata, or business-packet labels elsewhere.

Empty state text is factual:

- `READY TO PROBE KNOWN PROFILES` before scanning
- `NO PROTOCOL EVIDENCE YET` while a pass is running
- `NO EVIDENCE IN THIS PASS` after a quiet pass

None claims that a band, profile, or network is absent.

## Keyboard And Touch Interaction

| Input | Action |
| --- | --- |
| `UP` / `DOWN` | Select an observed or confirmed profile |
| `ENTER` | Open the apply confirmation for the selected profile |
| `S` | Start or stop the probe |
| `ESC` / Back | Close the dialog or leave the page |
| Tap/click a result row | Select that profile |
| Tap/click bottom `SET` or `START/STOP` | Same action as the matching key |

The bottom bar reflects the active state by displaying `S START` before a
probe starts and `S STOP` while it owns the radio. Protocol-specific TX is an
automatic scheduler step; there is no generic Transmit, Ping all, or Broadcast
all action.

## Confirmation Overlay

Applying a result is intentionally not a one-key operation.

```
                  +----------------------------+
                  | APPLY PROTOCOL PROFILE?    |
                  | MT 433.775 MHz             |
                  | 125K  SF11  C4/8            |
                  | CONFIRMED VIA ROUTING ACK   |
                  | [ESC CANCEL] [ENTER APPLY] |
                  +----------------------------+
```

Only observed/confirmed profiles with a faithful persistent mapping expose
`SET`. The dialog states the evidence class, never the business packet type.
It does not restart probing, transmit a new probe packet, or rewrite
configuration until Apply is explicitly confirmed.

The full-screen scrim is warm dark brown at 50 percent opacity. The modal is
warm white with a 2 px amber border and 8 px radius, matching the existing
tracker confirmation treatment. `Cancel` owns initial focus. Arrow left/right
moves between the two actions; Enter applies only after Apply is focused.
