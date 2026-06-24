# MeshCore V2 Multibyte Paths Specification

This document defines Trail Mate's full implementation target for MeshCore
issue #60: multibyte paths plus a repeater policy that can refuse one-byte
legacy traffic. This is not an MVP. The implementation may be delivered in
vertical layers, but the accepted protocol surface is the complete mechanism
defined here.

## User Requirement

The GitHub issue asks for "multi byte paths" and a repeater option to "only
repeat multi byte messages." The stated reason is path hash collision pressure:
when more than 140 repeaters are visible, the one-byte MeshCore node hash is no
longer usable enough, and adding repeaters makes the situation worse unless
repeaters can stop amplifying one-byte messages.

For Trail Mate this means:

1. MeshCore packet and payload handling must support a multibyte path profile.
2. The multibyte profile is MeshCore payload version 2.
3. A repeater can be configured to forward only multibyte packets.
4. Phone/BLE protocol surfaces must carry the profile, otherwise the path cannot
   be displayed, cached, or reused correctly.

## Upstream MeshCore Analysis

The source under `.tmp/MeshCore` reserves the protocol shape for V2, but does not
implement V2 data routing. Trail Mate must therefore implement the missing
mechanism deliberately rather than simply toggling a constant.

### Packet Version And Wire Envelope

Upstream `src/Packet.h` defines the packet header as route bits, payload type
bits, and payload version bits. The version values are:

| Version Bits | Name | Upstream Meaning |
|--------------|------|------------------|
| `0x00` | `PAYLOAD_VER_1` | 1-byte source/destination hashes, 2-byte MAC |
| `0x01` | `PAYLOAD_VER_2` | Future, with upstream comment suggesting 2-byte hashes and 4-byte MAC |
| `0x02` | `PAYLOAD_VER_3` | Future |
| `0x03` | `PAYLOAD_VER_4` | Future |

Upstream `src/Packet.cpp` writes the packet envelope as:

1. `header`
2. optional 4 bytes of transport codes
3. one byte `path_len`
4. `path_len` bytes of path
5. remaining payload bytes

The important point is that `path_len` is already a byte count, not a hop count.
This lets V2 use two bytes per hop without changing the packet envelope.

### V1 Hash And MAC Constants

Upstream `src/MeshCore.h` defines:

| Constant | Value | Meaning |
|----------|-------|---------|
| `PATH_HASH_SIZE` | `1` | Node hash is one byte |
| `CIPHER_MAC_SIZE` | `2` | Encrypted payload MAC is two bytes |
| `MAX_PATH_SIZE` | `64` | Path byte capacity |
| `MAX_PACKET_PAYLOAD` | `184` | Payload byte capacity |

Upstream `src/Identity.h` implements `copyHashTo()` by copying
`PATH_HASH_SIZE` bytes from the public key prefix. `isHashMatch()` compares the
same fixed width unless a caller explicitly passes a length.

### V2 Is Rejected For Normal Data

Upstream `src/Mesh.cpp` starts `Mesh::onRecvPacket()` by rejecting all packets
with `getPayloadVer() > PAYLOAD_VER_1`. Therefore all normal request, response,
text, ACK, group, anonymous request, returned path, and direct/flood routing
logic is V1-only today.

### TRACE Has A Local Variable-Width Hash Mechanism

Upstream direct `PAYLOAD_TYPE_TRACE` is the only partial precedent for
multibyte path hashes. Its flags use the lower two bits as `path_sz`; the next
hash width is `1 << path_sz` bytes, and the receiver checks
`Identity::isHashMatch(hash, width)`.

This is not the V2 data mechanism:

1. TRACE still rides in a packet that is otherwise accepted only by the
   V1 gate.
2. TRACE appends the route path to the payload and uses the packet `path` field
   for SNR collection.
3. Normal direct routing, flood routing, path returns, and payload crypto do not
   use this flag.

Trail Mate should reuse the idea that public-key prefixes can be compared at
variable widths, but V2 data packets must be represented by the packet payload
version, not by TRACE flags.

### Direct Routing Is Fixed To One-Byte Hops

Upstream direct routing checks whether the first `PATH_HASH_SIZE` bytes of
`packet->path` match this node. If so, it removes itself from the front of the
path and retransmits. Because `PATH_HASH_SIZE` is `1`, `removeSelfFromPath()`
contains a byte-shuffle implementation and has a compile-time error for wider
hash sizes.

Trail Mate cannot implement V2 by increasing a global hash size. It must make
direct routing profile-aware:

1. V1 direct packets consume one byte from the front of the path.
2. V2 direct packets consume two bytes from the front of the path.
3. `path_len` remains the path byte length.
4. A direct path is valid only when `path_len` is a multiple of the profile hash
   width.

### Flood Routing Is Fixed To One-Byte Appends

Upstream flood routing appends this node's hash by calling `copyHashTo()` and
increasing `path_len` by `PATH_HASH_SIZE`. Because `PATH_HASH_SIZE` is `1`,
V1 flood packets accumulate one byte per hop.

Trail Mate must make flood routing profile-aware:

1. V1 flood packets append one byte of the local public-key prefix.
2. V2 flood packets append two bytes of the local public-key prefix.
3. Flood duplicate/self-path detection must scan hops by the profile hash
   width, not by raw bytes.
4. Flood max checks must compare hop count when the setting is a hop limit, and
   also enforce `MAX_PATH_SIZE` as a byte capacity.

### Path Return Payloads Are Fixed To One-Byte Hashes

Upstream `createPathReturn()` builds a `PAYLOAD_TYPE_PATH` packet with:

1. destination hash prefix of `PATH_HASH_SIZE`
2. source hash prefix of `PATH_HASH_SIZE`
3. encrypted plaintext containing `path_len`, path bytes, `extra_type`, and
   optional `extra`

Because the packet payload version is V1 and `PATH_HASH_SIZE` is one, both the
outer peer-addressing hashes and the returned path are V1-only.

Trail Mate V2 path returns must set payload version 2 and use:

1. two-byte destination hash
2. two-byte source hash
3. four-byte cipher MAC
4. plaintext `path_len` as a byte count
5. returned path bytes that are a multiple of two
6. any bundled `extra` payload interpreted under the same profile unless that
   payload has its own explicit envelope

### Repeater Policy Exists But Is Not Profile-Aware

Upstream repeaters implement `allowPacketForward()` with:

1. disabled forwarding check (`disable_fwd`)
2. flood max check (`packet->path_len >= flood_max`)
3. optional region/transport checks

The CLI exposes this as `repeat on/off` and `flood.max`. There is no upstream
profile filter and no "repeat only multibyte" option.

Trail Mate must add this as an explicit forwarding policy. The user's required
behavior is strict: `MultibyteOnly` limits both flood repeat and direct
forwarding. A repeater in this mode must not be used as a V1 relay.

### Collision Model

Upstream documentation acknowledges one-byte public-key-prefix collision as a
known MeshCore limitation. The FAQ suggests generating a different private key
to obtain a desired first byte. That workaround does not scale to dense repeater
visibility. V2 reduces this pressure by moving both routing paths and encrypted
peer addressing from an 8-bit prefix to a 16-bit prefix.

## Trail Mate Current Baseline

Trail Mate currently mirrors the upstream V1 limitation in several layers:

| Area | Current Behavior |
|------|------------------|
| Packet parsing | Extracts payload version but runtime accepts only version 1 |
| Identity | `selfHash()` returns only `pub_[0]` |
| Peer payload helpers | Assume `dest_hash(1) + src_hash(1) + MAC(2)` |
| Group payload helpers | Assume `channel_hash(1) + MAC(2)` |
| Anonymous request helpers | Assume `dest_hash(1) + pubkey(32) + MAC(2)` |
| Flood routing | Appends one local hash byte and scans path byte-by-byte |
| Direct routing | Compares/removes one leading path byte |
| Path return | Stores and reuses path bytes without a profile |
| Route cache | Caches peer hash and path bytes without hash-width metadata |
| UI/settings | Exposes repeat and flood max but no path profile or repeat profile |
| BLE/phone protocol | Sends path bytes and path length without profile/hash width |

Any implementation that only sends V2 but keeps route cache, direct forwarding,
or BLE as V1-shaped is incomplete.

## Trail Mate V2 Definition

Trail Mate formally defines MeshCore payload version 2 as:

| Property | V1 | V2 |
|----------|----|----|
| Payload version bits | `0` | `1` |
| Path hash width | 1 byte | 2 bytes |
| Peer destination hash | 1 byte | 2 bytes |
| Peer source hash | 1 byte | 2 bytes |
| Group/channel hash | 1 byte | 2 bytes |
| Anonymous destination hash | 1 byte | 2 bytes |
| Cipher MAC size | 2 bytes | 4 bytes |
| `path_len` meaning | byte count | byte count |
| Max path bytes | 64 | 64 |
| Max path hops | 64 | 32 |

The V2 hash bytes are the first two bytes of the Ed25519 public key for node
identity, and the first two bytes of the existing channel hash for group
payloads. V1 remains first byte only.

## Profiles

Trail Mate uses two separate concepts:

1. Payload profile: the wire shape of a packet already received or about to be
   transmitted.
2. Send profile preference: the local policy for choosing a payload profile.

### Payload Profiles

| Name | Version | Hash Bytes | MAC Bytes |
|------|---------|------------|-----------|
| `V1` | `0` | `1` | `2` |
| `V2` | `1` | `2` | `4` |

The payload profile is derived from the packet version bits for received frames.
Code must not infer profile from path length alone.

### Send Profile Preferences

| Name | Meaning |
|------|---------|
| `AutoPreferV2` | Default. Send V2 when the destination, route cache, and local protocol surface can represent V2. Fall back to V1 only when a known constraint requires it. |
| `V1Only` | Send legacy V1 packets. This is compatibility mode. |
| `V2Only` | Send only V2 packets. If the destination cannot be represented as V2, sending fails or rediscovery is required. |

The default must be `AutoPreferV2`.

### Forwarding Policies

| Name | Meaning |
|------|---------|
| `Any` | Forward V1 and V2 according to normal repeat, flood max, duplicate, and route checks. |
| `MultibyteOnly` | Forward only packets whose payload profile is V2 or later with hash width greater than one. This applies to both flood repeat and direct forwarding. |

`MultibyteOnly` is a forwarding policy, not merely a flood option. If a V1
direct packet has this node as next hop, a MultibyteOnly repeater must drop it.

## Wire Format

### Packet Envelope

The packet envelope is unchanged:

| Field | Size | V2 Interpretation |
|-------|------|-------------------|
| `header` | 1 byte | Version bits are `PAYLOAD_VER_2` |
| `transport_codes` | optional 4 bytes | unchanged |
| `path_len` | 1 byte | path byte count |
| `path` | `path_len` bytes | two bytes per hop |
| `payload` | remaining bytes | payload type-specific V2 layout |

The parser must reject malformed V2 paths when `path_len % 2 != 0`.

### Peer Encrypted Payloads

For `REQ`, `RESPONSE`, `TXT_MSG`, and `PATH`, V2 payload bytes are:

| Field | Size |
|-------|------|
| destination hash | 2 bytes |
| source hash | 2 bytes |
| cipher MAC | 4 bytes |
| ciphertext | remaining bytes |

The encrypted plaintext structures remain type-specific and unchanged except
where they contain path bytes. Returned path plaintext keeps its one-byte
`path_len` field as a byte count.

### Anonymous Request

For `ANON_REQ`, V2 payload bytes are:

| Field | Size |
|-------|------|
| destination hash | 2 bytes |
| sender public key | 32 bytes |
| cipher MAC | 4 bytes |
| ciphertext | remaining bytes |

### Group Payloads

For `GRP_TXT` and `GRP_DATA`, V2 payload bytes are:

| Field | Size |
|-------|------|
| channel hash | 2 bytes |
| cipher MAC | 4 bytes |
| ciphertext | remaining bytes |

### ACK, Advert, Control, Trace, Multipart, Custom

Payload version still belongs to the packet envelope, but not every payload type
has embedded peer hashes or MACs.

| Payload Type | V2 Handling |
|--------------|-------------|
| `ACK` | Version may be V1 or V2. Packet hash and duplicate semantics remain unchanged. |
| `ADVERT` | Advertisement body remains unchanged. The route path, if any, follows the packet profile. |
| `CONTROL` | Body remains unchanged. Profile matters for routing only. |
| `TRACE` | Existing TRACE hash-size flags remain TRACE-specific. V2 route paths still use the packet profile. |
| `MULTIPART` | Outer packet profile determines route/hash/MAC interpretation of the embedded or reconstructed payload. |
| `RAW_CUSTOM` | Body is application-defined. Routing path still follows the packet profile. |

## Routing Semantics

### Profile-Aware Path Helpers

All routing code must use helpers equivalent to:

1. `hashBytes(profile)`
2. `macBytes(profile)`
3. `version(profile)`
4. `pathHopCount(profile, path_len)`
5. `pathIsWellFormed(profile, path_len)`
6. `copySelfHash(profile, dest)`
7. `hashMatches(profile, hash, public_key)`

Raw constants such as `1`, `2`, or `4` must not be spread through routing,
payload, or BLE code as protocol truth.

### Flood Routing

On V2 flood receive:

1. Reject if the path byte length is not a multiple of two.
2. Reject if this node's two-byte hash already appears in the path.
3. If the packet is for this node, process it and mark it not to retransmit.
4. If forwarding is allowed, append this node's two-byte hash.
5. Enforce `MAX_PATH_SIZE` as bytes.
6. Enforce the configured flood max as hops.
7. Retransmit with the same payload version.

### Direct Routing

On V2 direct receive:

1. Reject if `path_len` is non-zero and not a multiple of two.
2. If `path_len == 0`, this is a zero-hop direct packet for local processing.
3. If the first two path bytes do not match this node, drop.
4. If forwarding policy is `MultibyteOnly`, allow this V2 path; if the packet is
   V1, drop.
5. Remove the leading two bytes from the path.
6. Retransmit with the same payload version.

### Returned Path Reuse

A returned path must be cached with:

1. payload profile
2. hash width
3. peer hash bytes
4. outbound path bytes
5. path byte length
6. hop count derived from profile
7. quality/age metadata

When sending direct, Trail Mate must only reuse a cached path whose profile is
compatible with the selected send profile. `AutoPreferV2` should prefer V2
cached routes and fall back to V1 routes only when no V2 route exists and the
destination has not been marked V2-required.

## Settings And Persistence

Trail Mate must persist:

1. send profile preference, default `AutoPreferV2`
2. forwarding policy, default `MultibyteOnly`
3. existing repeat enabled/disabled setting
4. existing flood max setting

The old repeat switch still controls whether this node repeats at all. The new
forwarding policy controls which packet profiles may be repeated when repeat is
enabled.

Settings backup/restore must include the new values with stable JSON keys.

## BLE And Phone Protocol Extension

BLE/phone external protocol must be extended. A path without profile metadata is
ambiguous because the same byte array has a different hop count and different
hash display depending on profile.

Every contact/path transfer that currently contains `out_path_len` and
`out_path` must also contain:

1. path profile/version
2. hash width
3. peer hash width/profile for displayed short hash
4. enough versioning or feature bits for older phone clients to reject or ignore
   the extension safely

The phone-visible representation must display V2 paths as two-byte hop hashes
and must send V2 route reuse commands back with the same profile. A phone command
that sends a path without profile metadata is V1 by definition and must not be
used to transmit V2 paths.

## Compatibility

V1 is still accepted unless local forwarding policy says otherwise. This keeps
Trail Mate compatible with existing MeshCore nodes.

V2 packets should be emitted by default for Trail Mate-originated peer/group
traffic when enough local information is available. Because upstream MeshCore
currently rejects `PAYLOAD_VER_2`, `AutoPreferV2` may fall back to V1 for known
legacy peers and legacy phone-provided paths. The fallback must be explicit in
the selected send profile logic, not an accidental consequence of missing V2
code.

## Implementation Plan

The implementation should be layered, but each layer must preserve the final
model:

1. Add protocol profile primitives and unit tests for V1/V2 hash width, MAC
   width, version, path validation, hop count, and hash formatting.
2. Make payload helpers parse and build V1 and V2 peer, anonymous, and group
   shapes.
3. Make identity helpers provide one-byte and two-byte public-key prefixes.
4. Make packet receive, flood forwarding, direct forwarding, duplicate/self path
   checks, timeout/hop estimates, and path return creation profile-aware.
5. Extend route cache and peer info to store route profile and peer hash bytes.
6. Add send profile selection with default `AutoPreferV2`.
7. Add forwarding policy with `MultibyteOnly` applying to flood repeat and direct
   forwarding.
8. Make self-announcement/ADVERT frame generation profile-aware so default
   device announcements no longer remain an accidental V1 island.
9. Extend platform backends that expose MeshCore raw/phone-facing sends. The
   ESP adapter owns full V2 route/cache/forwarding semantics; the nRF52 radio
   adapter must at least use explicit profile headers, V2 peer/anonymous
   request hashes/MACs when pubkeys are available, and `sendRawDataEx` for
   external V2 path reuse.
10. Extend settings persistence, UI settings, and backup/restore.
11. Extend BLE/phone path/contact protocol so V2 paths can be displayed and
   reused.
12. Update docs and tests.

## Burn-Down Criteria

The issue is not complete until all of these are true:

1. V2 packets with `2-byte hash + 4-byte MAC` are parsed, built, received, and
   transmitted.
2. V2 flood paths append two-byte hashes and enforce both byte capacity and hop
   flood max.
3. V2 direct paths match/remove two-byte leading hops.
4. V1 forwarding is blocked for both flood and direct paths when
   `MultibyteOnly` is configured.
5. Route/path cache entries retain profile metadata and direct sends do not
   reuse V1/V2 paths ambiguously.
6. `AutoPreferV2` is the default send preference.
7. BLE/phone protocol can display and reuse V2 paths correctly.
8. Existing V1 compatibility tests continue to pass.
9. New unit tests cover V1/V2 payload shape, routing path math, forwarding
   policy, route cache profile separation, and BLE profile serialization.
10. The implementation has passed the repository's relevant build/test commands.
    GitNexus `detect_changes` must run before any commit when the index is
    available; if GitNexus is unavailable after an attempted refresh, record
    that explicitly and proceed under the user-approved bypass.
