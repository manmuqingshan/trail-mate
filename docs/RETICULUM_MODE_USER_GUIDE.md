# Reticulum Mode User Guide

This document describes the user-facing behavior of Trail Mate when the active
protocol is `Reticulum`. It focuses on the parts that a field user needs to
configure, especially shared group destinations stored on the SD card.

## Current Product Model

Trail Mate treats `Reticulum` as a product protocol at the same level as
`Meshtastic` and `MeshCore`.

Reticulum mode can use more than one carrier interface at the same time:

- LoRa through the device's RNode-compatible raw LoRa carrier.
- Wi-Fi through a Reticulum TCP gateway using the upstream TCPInterface HDLC
  framing on port `4242` by default.

The Wi-Fi carrier is not MQTT, HTTP, or a separate Trail Mate protocol. It is
just another Reticulum interface under the same Reticulum runtime. Contacts,
Nearby, Groups, announces, path requests, proofs, links, and LXMF packets all
flow through the same Reticulum product layer, then fan out through the enabled
carrier interfaces.

By default, LoRa is enabled and the Wi-Fi gateway is disabled. To use Wi-Fi as
an additional Reticulum carrier, configure normal Wi-Fi credentials first, then
open Settings > Network while the active protocol is `Reticulum` and set:

- `Reticulum LoRa`: keep enabled when LoRa should remain active.
- `Wi-Fi Gateway`: enable the TCP gateway carrier.
- `Gateway Host`: the hostname or IP address of a Reticulum TCP server or
  backbone node.
- `Gateway Port`: usually `4242`.
- `Auto Wi-Fi`: when enabled, Reticulum will ask the Wi-Fi runtime to connect
  before opening the gateway socket.

Trail Mate prevents disabling both Reticulum LoRa and Reticulum Wi-Fi gateway
from the device UI, because that would leave Reticulum selected with no carrier
interface.

## Reticulum Identity

When the active protocol is `Reticulum`, Settings > Network shows the local
Reticulum identity facts that are useful when exchanging addresses with another
person:

- `Identity Hash`: the local Reticulum identity hash.
- `LXMF Address`: the local LXMF delivery destination hash. Share this when
  someone needs to address this Trail Mate directly over LXMF.

The 16-byte Reticulum hashes are shown as 32 hexadecimal characters. On small
screens Trail Mate wraps them after 16 characters; the line break is only a
display wrap, not part of the address.

When the active protocol is `Reticulum`, the main menu also shows a `Network`
app with the Nomad-style network icon. That app is the Reticulum network view,
not the source of truth for local identity addresses.

## Anonymous Peer

Settings > Network includes `Anonymous Peer` while the active protocol is
`Reticulum`.

When enabled, Trail Mate keeps its local Reticulum identity and can still use
configured destinations, but it does not send local delivery or propagation
announces. It also suppresses local announce responses used for path discovery.
This makes the device less discoverable, but other nodes will not learn the
LXMF Address from normal Reticulum announce traffic. Disable `Anonymous Peer`
when you want ordinary peers to discover and message this device from Reticulum
announces.

In Reticulum mode, the Contacts page uses these filters:

- `Contacts`: saved Reticulum peers or contacts.
- `Nearby`: Reticulum peers learned from announces and path traffic.
- `Groups`: locally configured Reticulum shared group destinations.
- `Ignored`: nodes hidden from the normal contact lists.

`Groups` are not discovered from the Reticulum network. They are explicit local
configuration. This matters because a Reticulum shared destination must be known
before Trail Mate can send to it or accept inbound group packets for it.

## SD Card Requirement

Reticulum group configuration lives on the SD card.

The firmware ships with zero preconfigured Reticulum groups. Every group shown
on the device must come from this SD-card file or from the device Add workflow.
Reticulum groups are not stored in ESP NVS or the normal app settings store;
that prevents hidden groups from surviving outside the SD-card source of truth.

If no SD card is available, Trail Mate must not silently use built-in default
groups. The Groups list will show an Add row, but tapping Add reports that an
SD card is required. Internally, the runtime group table is cleared when the SD
configuration cannot be loaded.

The SD card file path is:

```text
/trailmate/reticulum/groups.tsv
```

When editing the card from a computer, create this path relative to the SD card
root:

```text
trailmate/reticulum/groups.tsv
```

## TSV File Format

The file is a UTF-8 text file using tab-separated values. The parser is small on
purpose so it remains reliable on ESP-class devices.

Each group line has exactly three fields:

```text
enabled<TAB>name<TAB>destination_hash
```

Do not type the literal string `<TAB>`. Press the Tab key or insert an actual
tab character between fields.

### Field Rules

`enabled`

- `1`, `true`, `yes`, and `enabled` mean the group is active.
- Any other value is treated as disabled.
- Disabled groups remain valid file entries, but they are not shown in the
  Groups chat list.

`name`

- This is the display name shown in Contacts > Groups.
- Maximum length is 31 bytes.
- ASCII names are safest. Short UTF-8 names may work, but remember that Chinese
  characters usually use 3 bytes each.
- The name must not contain tabs.

`destination_hash`

- This is a 16-byte Reticulum destination hash written as 32 hexadecimal
  characters.
- Uppercase and lowercase hex are both accepted.
- Spaces, `:`, `-`, and `_` inside the hash are ignored by the parser, but the
  clean 32-character form is recommended.
- This must be the shared group destination hash that the other Reticulum node
  is also listening on. A normal LXMF peer delivery hash belongs in Nearby or
  Contacts single-peer chat, not in the Groups TSV unless that host has
  explicitly configured the same hash as a shared group destination.

## Example

```text
# Trail Mate Reticulum groups
version	1
1	Kunming Team	0123456789ABCDEF0123456789ABCDEF
1	Home Node	FEDCBA9876543210FEDCBA9876543210
0	Test Lab	00112233445566778899AABBCCDDEEFF
```

Notes:

- Lines starting with `#` are comments.
- Blank lines are ignored.
- `version<TAB>1` is optional, but recommended.
- Trail Mate currently supports up to 4 configured Reticulum groups.
- Invalid group lines are skipped and logged as `[RTGroupConfig] skip invalid line`.

## Import Workflow

1. Power off Trail Mate or safely expose/remove the SD card.
2. On a computer, create the folder:

   ```text
   trailmate/reticulum
   ```

3. Create or edit:

   ```text
   trailmate/reticulum/groups.tsv
   ```

4. Write one group per line using the TSV format above.
5. Insert the SD card and boot Trail Mate.
6. Switch the active protocol to `Reticulum`.
7. Open Contacts > Groups.

The Groups list refreshes from the SD file. Re-entering the Contacts page, changing
protocol, or applying mesh config also reloads the SD group table.

## Adding From The Device UI

Contacts > Groups contains an Add row above the configured groups.

Tapping Add opens a Reticulum group configuration page with:

- `Name`
- `Destination hash`

Saving writes the SD-card TSV file immediately and reapplies the Reticulum mesh
configuration. This UI is useful for small edits, but entering a 32-character
hash on-device can be tedious. For repeatable setup, editing the TSV file on a
computer is the preferred workflow.

## Location And Realtime Sharing

Reticulum mode supports Trail Mate's team location business through LXMF
appdata. This is different from Meshtastic's native `POSITION_APP` packets.

When the active protocol is `Reticulum` and Team keys are available:

- Contacts > Team can send a current-location message.
- The team realtime track sampler can send `TEAM_TRACK_APP` updates.
- Incoming `TEAM_POSITION_APP` and `TEAM_TRACK_APP` packets are decoded by the
  Team service and shown through the Team UI/map stores.

Those packets are encrypted with the Trail Mate Team keys and carried inside
Reticulum/LXMF appdata. Other Trail Mate devices that know the same Team keys
can consume them. Generic Reticulum clients such as NomadNet or MeshChat may
see ordinary LXMF traffic, but they will not understand Trail Mate team
position payloads unless they implement the same Trail Mate appdata subset.

Reticulum Groups are LXMF shared destination chats. They currently carry text
messages. They do not currently send Trail Mate rich-location appdata to a
specific Reticulum group destination. That requires a future appdata send path
that can address a full Reticulum destination hash, or an explicit text-based
location format such as a `geo:` URI. This boundary avoids making Team
location sharing look like it was sent to a Reticulum group when it actually
uses the Team appdata channel.

## Troubleshooting

`SD card required`

Trail Mate could not see a mounted SD card. Insert a card and re-enter Contacts
> Groups or reboot.

`No Reticulum groups`

The SD card is present, but `/trailmate/reticulum/groups.tsv` does not exist or
contains no enabled valid groups.

`Destination must be 32 hex chars`

The destination field did not decode to exactly 16 bytes. Remove extra text and
make sure the field contains 32 hexadecimal characters.

`Group already exists`

The same destination hash is already present in the current group table.

Messages sent from Groups are not received by another node

Make sure the other node is listening on the same shared group destination hash.
Ordinary LXMF delivery peers are handled by Nearby or Contacts single-peer chat.
Groups require shared destination configuration on both sides.

## Current Boundary

This file configures Trail Mate's supported Reticulum group subset. It is not a
complete Reticulum configuration file and does not configure full upstream
Reticulum propagation node policy.

Trail Mate's Wi-Fi carrier currently implements the standard Reticulum
TCPInterface packet framing so it can exchange raw Reticulum packets with a
configured Reticulum TCP gateway. It does not use the Meshtastic MQTT proxy
path.
