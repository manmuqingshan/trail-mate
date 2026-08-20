# Protocol Probe

## Status And Intent

This is the authoritative target specification for the user-facing feature now
n
RSSI-oriented Energy Sweep design.

The internal application id and source directory remain `energy_sweep` for
navigation and migration compatibility. The visible feature uses `radar.c`;
`Spectrum.c` is not part of this feature.

The implementation follows the product contract below: a finite queue of
complete profiles, protocol-level evidence only, and protocol-specific
confirmation windows. It does not retain the former 25 kHz grid or fixed
700 ms dwell.

## Purpose

Protocol Probe answers one question: which complete LoRa air profiles carry
real MeshCore, Meshtastic, or Reticulum traffic, and which MeshCore or
Meshtastic profiles can be actively confirmed to communicate with a peer?

It does not measure spectrum occupancy, identify a quietest channel, recommend
a low-noise frequency, or infer a protocol from RSSI alone.

## Air Profile And Protocol Context

An air profile is a complete PHY hypothesis:

```text
protocol, frequency, bandwidth, spreading factor, coding rate,
sync word, preamble, header mode, payload CRC mode
```

Frequency alone is not a candidate. The radio must be configured with a
compatible PHY to decode a packet.

Protocol context is deliberately separate. It holds the data needed for an
active check: MeshCore Discover mode or a Meshtastic channel key plus observed
node id. Reticulum has no active verification context in this release: the
page accepts only public, self-consistent discovery or control evidence. A
single PHY profile may carry more than one logical network, so observing it
does not imply that Trail Mate has credentials for every network using it.

## Evidence Contract

Evidence is ordered and never collapsed into a binary result:

| Level | Internal finding | User-visible result | Meaning |
| --- | --- | --- | --- |
| E0 | RF activity | Not a profile result | CAD/RF activity is a weak hint only. |
| E1 | CRC-passing LoRa frame | Diagnostics only | The frame is not yet attributable to a target protocol. |
| E2 | Protocol observed | `OBSERVED` | Protocol-specific parsing accepted MC, MT, or RT. Its strength depends on protocol context; it is not an active reachability claim. |
| E3 | Active verification | `CONFIRMED` | A correlated protocol response or cryptographic proof was received. |

Only E2 and E3 results are listed or selectable. Rows show an evidence count
but never classify business payloads such as position, node info, telemetry, or
message text. No simulator path may create evidence.

A missing packet, ACK, Discover response, or Proof is inconclusive. It never
means a profile is absent, unused, invalid, or safe to remove.

## Candidate Plan And Timing

Candidates are finite, complete profiles ordered by the confidence of their
source. `Known` means Trail Mate can explain the hypothesis source, not that
it has been confirmed.

1. The actually applied profile is first. Meshtastic uses the derived frequency,
   including override frequency and frequency offset.
2. Meshtastic standard profiles are derived from the same complete radio
   configuration with only the modem preset changed: region, configured
   channel name or channel number, override frequency, frequency offset, and
   power limits remain in effect.
3. MeshCore profiles are derived from its supported regional/profile settings.
4. Reticulum profiles come from the configured RNode interface. RNode PHY
   parameters are independently configurable, so RT has no universal regional
   candidate list.

Changing only a Meshtastic modem preset must never replace the configured
channel identity with a preset display name. Doing so would generate a
plausible-looking but incorrect candidate frequency and turn the probe into a
scan of the wrong channel.

Within an open page session, observed profiles receive a longer dwell on later
passes. Every candidate also remains in RX for at least the conservative
airtime of one maximum-size explicit-header LoRa frame for that candidate's
`BW/SF/CR`, plus a tuning guard. This is essential for slow profiles: a fixed
2.2-second visit can leave before a valid `SF12/BW125` frame has completed.
Slow candidates therefore make a complete pass take materially longer. That is
an intentional reliability trade-off, not a negative channel finding.
For MeshCore, the active Discover response window also scales with the current
candidate. Peers use a randomized airtime-based response schedule, so a slow
`SF11` or `SF12` candidate can need tens of seconds after the Discover
transmission before its response is due. A fixed five-second timeout would
incorrectly retune before a compliant peer had answered.
Persistent profile history and user-imported hypotheses are deliberately not
part of this release; they need their own storage and provenance UX before they
can be presented as trusted candidates.

There is no automatic full-band 25 kHz sweep. An explicit advanced mode may
accept user-specified profiles, but it must call them best-effort hypotheses and
must not make absence claims.

The scheduler gives the active profile a sustained receive window, then walks
the remaining queue by priority. Evidence extends and prioritises a candidate
for revisit. Response windows are protocol-specific; a global fixed dwell is
invalid because packet airtime, routing delay, and Proof generation vary.

## Protocol-Specific Routes

### MeshCore

```text
candidate MeshCore profile
  -> rate-limited Discover
  -> receive through the protocol response window
  -> valid Discover response or ACK
  -> CONFIRMED
```

A structurally valid passive MeshCore packet with a known MeshCore payload
shape is E2. The probe rejects unknown/custom payload types and malformed
known payloads even when their LoRa frame happens to pass CRC. Only a Discover
response carrying this probe's tag upgrades the same profile to E3; a generic
MeshCore ACK has no Discover-tag correlation and remains passive evidence. A
silent response window is `NO RESPONSE`, not rejection.

### Meshtastic

Meshtastic must not broadcast NodeInfo or user information and expect an ACK.
The active confirmation is a targeted unicast route:

```text
candidate MT profile
  -> passively receive a structurally valid MeshPacket
  -> record the observed source node id
  -> only with a matching usable channel context/key:
       send small user-invisible unicast MeshPacket with want_ack=true
  -> correlated ROUTING_APP acknowledgement
  -> CONFIRMED
```

Without the channel key, Trail Mate may retain E2 but must not claim it can
actively use that network. No ACK remains inconclusive: the peer may sleep, be
unroutable, be rate-limited, or use a different key.

For a received MT packet whose channel hash matches a locally configured
channel, E2 requires both successful decryption and a valid non-zero
`meshtastic_Data.portnum`. This prevents arbitrary LoRa bytes that happen to
resemble the 16-byte MT outer header from becoming evidence. A packet on an
unknown MT channel cannot be decrypted, so its outer header is E1 diagnostics
only and never creates a selectable result. The user must configure matching
channel context and receive a decryptable frame before the probe can claim E2
or attempt E3.

### Reticulum / RNode

RT is traffic-driven. A LoRa interface carrying active Reticulum backbone
traffic can yield Announce, Path Request, Data, Link Request, or Proof frames.
For passive E2, the probe accepts only a self-consistent Announce or a Path
Request directed at Reticulum's fixed control destination. An Announce must
derive the same destination hash from its included public key and name hash;
the Path Request control destination is a fixed 128-bit protocol value. The
page does not label either packet type in the UI.

Reticulum's air header intentionally has neither a magic constant nor a
universal authentication tag: encrypted Data, Link Request, and Proof frames
cannot be attributed without their destination identity. Therefore they do not
create a profile result on this page. RT E2 means "a self-consistent public
Reticulum discovery/control frame was received on this exact configured RNode
PHY", not proof that every frame belongs to one particular Reticulum network.
Frames that carry an optional interface access code (IFAC) are also excluded:
Protocol Probe has no matching access-code context while it is temporarily
tuned, so it must not interpret the post-IFAC bytes as a normal Reticulum
header. RT remains passive and cannot claim E3 without a separate identity-aware
network operation after the profile is applied.

```text
candidate RT profile
  -> passively parse Reticulum traffic
  -> OBSERVED
```

When a Reticulum backbone is connected, it may carry announces, path requests,
data, links, and proofs. This page converts only Announce and Path Request into
evidence; a network that emits only encrypted Data, Link, or Proof traffic will
intentionally produce no RT result.
Protocol Probe intentionally does not run Reticulum Path Request or Ping while
it owns an exclusive, temporarily tuned radio lease: those are normal-network
operations that require the configured Reticulum runtime, its path cache, and
its asynchronous Proof correlation. After a selected RT profile is applied,
the existing chat Ping remains the separate way to verify a known destination.

## Active-Transmission Policy

Active actions are protocol routes, not a generic radio scan:

- MeshCore Discover is rate-limited to one attempt per candidate in each full
  pass. Its receive window covers the protocol's randomized response schedule
  for that PHY, including the full response airtime and a guard. A confirmed
  profile is not retried; an unconfirmed profile can be retried on the next
  pass so a lost reply, sleeping node, or transient collision remains
  inconclusive instead of becoming a permanent miss.
- MeshCore and Meshtastic both honor their configured `tx_enabled` flag. With
  transmission disabled, Protocol Probe continues passive evidence collection
  but emits no confirmation probe.
- Meshtastic sends only after passive evidence provides a target node and a
  usable channel context.
- Reticulum has no active probe in this page; it remains passive and does not
  inject untargeted discovery traffic across candidates.

The phase line shows the active profile while receiving, for example
`MT 433.775 B125 S11 3/12`, and shows `TX` during an active step. This makes a
controlled test deterministic: traffic can be sent while the exact candidate
is visible instead of relying on blind timing. The scheduler starts the
response/proof window only after transmission completes and RX has resumed,
then remains in RX before retuning. A negative active result is retained as
diagnostics and never erases passive evidence.

Each active transmission uses the same candidate LoRa packet parameters as
its receive window, including preamble length and payload-CRC mode. A request
that changes those parameters while transmitting is not a valid confirmation
of the candidate profile.

## Radio Ownership And Apply Flow

The probe obtains the exclusive LoRa runtime only while active. On each RX_DONE
it copies the packet into caller-owned scratch storage, clears IRQ flags, and
restarts reception. Arduino and ESP-IDF both quiesce the normal radio and mesh
tasks before retuning; this prevents the normal adapter from clearing an IRQ or
restarting RX during a probe frame. Leaving, stopping, applying, or losing the
lease reapplies the configured mesh PHY before returning the radio to mesh
ownership and resuming only the tasks that the probe itself paused.

The RX poller treats preamble, sync-word, and valid-header IRQs as in-progress
signals. It clears those sticky notifications without restarting the receiver;
only RX completion, CRC/header failure, or timeout restarts RX. Restarting on
every non-RX_DONE IRQ would abort a real long packet before its payload can be
read and would make a protocol probe appear silent. The Arduino RadioLib and
ESP-IDF SX126x runtime paths enforce the same terminal-IRQ rule.

Applying a selected result is an explicit two-step action:

1. The user selects an observed profile and opens the confirmation dialog.
2. On confirmation, the page stops probing, releases the temporary radio
   session, updates configuration in a `beginConfigEdit()` transaction, commits
   `AppConfigChangeSet::mesh()`, then calls `applyMeshConfig()`.

Meshtastic is switched to manual modem parameters with the observed center
frequency as its override and zero frequency offset. MeshCore and Reticulum use
their own supported persistent-profile mappings. A result without a faithful
mapping remains viewable but is not offered as an apply target.

## Hardware Capability Model

| Radio | Baseline | Product consequence |
| --- | --- | --- |
| SX1262 | One frequency/BW/SF/CR receive configuration at a time | Candidates are visited serially. |
| LR1121 | The IC can detect two LoRa SF values in parallel on a compatible frequency/BW configuration | A native-driver proof of concept may group two same-frequency/BW SF hypotheses. |

RadioLib's high-level receive API must not be assumed to expose LR1121 Multi-SF
attribution. Until a device-level proof of concept demonstrates configuration,
RX packet retrieval, and SF attribution, LR1121 follows the SX1262 single-lane
scheduler. Multi-SF improves passive MT/RT discovery only; a transmission still
uses one explicit profile.

## On-Air Acceptance

The acceptance test uses protocol traffic, not arbitrary CRC-valid LoRa bytes.
Watch the live phase line and transmit only while the intended candidate is
shown; the finite candidate queue deliberately does not listen on every
frequency or modem combination at the same time.

| Protocol | Required peer-side action | Expected evidence |
| --- | --- | --- |
| MeshCore | Keep at least one discover-capable peer on the candidate PHY while the page is scanning. | `CONFIRMED` only after a Discover response carrying this probe's tag. A passive MeshCore frame may first create `OBSERVED`. |
| Meshtastic | Have a configured-channel peer send a normal MeshPacket, then remain reachable for the targeted `want_ack` request. | The valid packet creates `OBSERVED`; a correlated encrypted `ROUTING_APP` ACK upgrades it to `CONFIRMED`. |
| Reticulum | Keep the RNode interface connected to a network that emits an Announce or Path Request. | A self-consistent Announce or fixed-control Path Request creates `OBSERVED`; this page does not claim E3 for RT. |

The test must not treat arbitrary packets, generic LoRa generators, or a
CRC-valid non-target frame as protocol evidence. Those inputs exercise the
radio's PHY but intentionally do not populate Protocol Probe results.

## Pager UI Specification

The 480x222 Pager page follows the Map page language:

- A 30 px amber top bar titled `PROTOCOL PROBE`.
- An unframed, full-width work area containing the observed-profile list only.
  The selected row is highlighted in place; there is no repeated detail panel,
  which keeps the page viable on T-Deck-class narrow displays.
- Rows show protocol tag, `frequency`, `BW/SF/CR`, evidence count, and
  `OBSERVED` or `CONFIRMED`.
- The phase line states the real action and active profile, for example
  `MT 433.775 B125 S11 3/12`, `MC TX 433.775`, or `RT TRAFFIC 18`.
- A 24 px Map-style bottom bar: `UP/DN Select`, `ENTER Set`, `S Start/Stop`,
  and `ESC Back`.
- Selecting `Set` opens a dimmed warm-white modal. `Cancel` is the default
  focus; `ESC` cancels and `ENTER` applies after the user has moved focus to
  Apply.

The page has no RSSI graph, CAD badge, noise estimate, quietest-channel score,
AUTO action, selected-detail panel, or business-packet classification. Those
measurements cannot establish protocol-channel presence.

## References

- [MeshCore packet format](https://docs.meshcore.io/packet_format/)
- [Meshtastic protocol and want_ack](https://github.com/meshtastic/meshtastic-sdk/blob/main/docs/protocol.md)
- [Reticulum announces, paths, and proofs](https://reticulum.network/manual/understanding.html)
- [Reticulum RNode interface configuration](https://reticulum.network/manual/interfaces.html)
- [RadioLib LR1121 API surface](https://jgromes.github.io/RadioLib/class_l_r1121-members.html)
