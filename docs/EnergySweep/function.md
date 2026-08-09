# Packet Probe

## Purpose

Packet Probe replaces the former RSSI-oriented Energy Sweep page. Its question is
not whether a frequency range contains energy. It asks whether an active Trail
Mate air protocol can be observed on a concrete set of LoRa air parameters.

The internal application id and source directory stay `energy_sweep` for
navigation and migration compatibility. The user-facing name is `Packet Probe`.

## Evidence Contract

An observed profile is created only when all of the following are true:

1. The radio reports RX_DONE and provides a packet that has passed the radio
   CRC path.
2. The receiver was configured with the candidate frequency, bandwidth,
   spreading factor, coding rate, preamble, sync word, and CRC length.
3. The packet has passed a structural parser for the active protocol:
   Meshtastic wire framing or MeshCore packet framing.

The page displays the profile and its evidence-packet count. It deliberately
does not expose business-packet labels such as position, node info, or telemetry.
No simulator path may produce evidence packets. A candidate with no evidence is
not a negative finding; it means only that the current dwell did not observe one.

The first version's evidence level is radio CRC plus structural protocol
validation. It is a strong discovery signal but is not authenticated proof of a
channel key. A future evidence-level upgrade can use the active adapter's full
decrypt/MAC validation while preserving the same UI result model.

## Candidate Plan And Timing

Candidates are generated from the active protocol's regional frequency span and
currently active LoRa parameters. The center frequencies are quantized to 25
kHz, limited to 96 candidates, and adjusted to keep the configured bandwidth in
the regional range.

Each candidate receives a 700 ms dwell. A typical 70-candidate pass is about 49
seconds before radio retuning overhead. One valid packet is an immediate positive
result. Multiple passes increase coverage for intermittent traffic, but the UI
must never label a silent candidate as absent or unused.

## Radio Ownership And Apply Flow

The probe obtains the exclusive LoRa runtime only while scanning. On each
RX_DONE it copies the packet into caller-owned scratch storage, clears IRQ flags,
and restarts reception. Leaving, stopping, or applying releases the runtime and
returns the radio to mesh ownership.

Applying a selected result is an explicit two-step action:

1. The user selects an observed profile and opens the confirmation dialog.
2. On confirmation, the page stops probing, releases the temporary radio
   session, updates configuration in a `beginConfigEdit()` transaction, commits
   `AppConfigChangeSet::mesh()`, then calls `applyMeshConfig()`.

Meshtastic is switched to manual modem parameters with the observed center
frequency as its override and zero frequency offset. MeshCore switches to its
custom region values with the observed frequency, bandwidth, SF, and CR.

## Hardware Capability Model

| Radio | Current implementation | Result |
| --- | --- | --- |
| SX1262 | One frequency/BW/SF/CR receive configuration at a time | One single-SF lane scans candidates serially. |
| LR1121 | The IC supports Multi-SF in its datasheet, but local RadioLib 7.4 integration exposes only normal single-profile receive and no packet-to-SF attribution API | The first release intentionally uses the same one-lane model. |

The probe scheduler is profile-based so a future LR1121 POC can add a second
same-frequency/BW SF lane. Enabling that needs all of the following real-device
proof: RadioLib configuration support, RX_DONE packet retrieval for both SFs,
and reliable attribution of each received packet to its SF. Until then the UI
and evidence count make no multi-SF claim.

## Pager UI Specification

The 480x222 Pager page follows the Map page language:

- A 30 px amber top bar titled `PACKET PROBE`.
- An unframed, full-width work area containing the observed-profile list only.
  The selected row is highlighted in place; there is no repeated detail panel,
  which keeps the page viable on T-Deck-class narrow displays.
- Observed rows show `frequency`, `BW/SF/CR`, and `x evidence-count` only.
- A 24 px Map-style bottom bar: `UP/DN Select`, `ENTER Set`, `S Start/Stop`,
  and `ESC Back`.
- Selecting `Set` opens a dimmed warm-white modal. `Cancel` is the default
  focus; `ESC` cancels and `ENTER` applies after the user has moved focus to
  Apply.

The page has no RSSI graph, CAD badge, noise estimate, quietest-channel score,
or AUTO action. Those measurements cannot establish protocol-channel presence.
