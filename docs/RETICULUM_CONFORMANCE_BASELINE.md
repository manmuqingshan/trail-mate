# Reticulum Conformance Baseline

## Purpose

This document defines how Trail Mate validates its embedded Reticulum behavior
without making `microReticulum` a product runtime dependency.

The goal is not to copy another implementation's internal class layout. The
goal is to keep Trail Mate's Reticulum work aligned with protocol behavior while
we continue to separate user-facing protocol choices from historical carrier and
message-layer details.

## Reference Hierarchy

Trail Mate uses a two-reference model:

| Reference | Role | Authority |
| --- | --- | --- |
| Python Reticulum | Canonical protocol behavior | Highest |
| `attermann/microReticulum` | MCU/C++ reference implementation | Secondary |
| Trail Mate Reticulum code | Product implementation under test | Subject |

If Python Reticulum and `microReticulum` disagree, the disagreement must be
recorded before Trail Mate treats either behavior as binding.

As of 2026-07-06, the inspected `microReticulum` reference is release `0.5.0`
from 2026-06-27. Test fixtures must pin the exact upstream commit they were
generated from.

## Product Boundary

The user-facing protocol set is:

| User-facing protocol | Meaning |
| --- | --- |
| Meshtastic | Native Meshtastic backend |
| MeshCore | Native MeshCore backend |
| Reticulum | Device-owned Reticulum network stack over available interfaces |

The following names are not user-facing protocol choices:

| Name | Correct role |
| --- | --- |
| RNode | Reticulum-capable LoRa carrier/interface or external modem role |
| LXMF | Reticulum message/service layer |
| PC Link | Legacy host bridge feature targeted for removal |
| MQTT | IP/Wi-Fi expansion transport for applicable backends |

Conformance work must not re-promote RNode, LXMF, or PC Link into the primary
protocol selector.

## Scope

This baseline covers Reticulum network-stack behavior:

- identity hash and destination hash derivation
- packet header encoding and parsing
- packet hash and truncated packet hash derivation
- announce layout, validation, caching, and replay behavior
- path learning and path request behavior
- packet duplicate filtering
- proof generation, validation, receipt handling, and reverse-path routing
- link establishment, proof, keepalive, close, and timeout behavior
- resource advertisement, hashmap, request, part, proof, and completion behavior

This baseline does not use `microReticulum` to validate:

- Trail Mate team membership semantics
- TeamKey trust and verification rules
- chat read-model projection
- UI protocol labels and settings layout
- full LXMF messaging semantics beyond the product-supported wire subset
- LXMF propagation-node service interoperability beyond the
  product-supported wire subset

LXMF-specific checks need a separate reference pass against Python LXMF or an
interoperable LXMF client such as Sideband.

## Deviation Classification

Every mismatch found during conformance work must be classified before it drives
implementation changes.

| Class | Meaning | Default action |
| --- | --- | --- |
| `bug` | Trail Mate contradicts the reference behavior in supported scope | Fix |
| `intentional-subset` | Trail Mate deliberately supports a smaller embedded subset | Document and test |
| `missing-feature` | Behavior is valid Reticulum but outside the current implementation | Defer with owner |
| `product-extension` | Trail Mate adds behavior above Reticulum, such as team appdata | Keep outside protocol core |
| `reference-disagreement` | Python Reticulum and `microReticulum` differ | Investigate before fixing |
| `upstream-gap` | `microReticulum` lacks the feature needed for comparison | Use canonical reference |

## Initial Deviation Ledger

| ID | Area | Observation | Class | Decision |
| --- | --- | --- | --- | --- |
| RCNF-001 | Protocol selector | User-facing choices are Meshtastic, MeshCore, and Reticulum; legacy `RNode` and `LXMF` values remain compatibility/internal details | product-extension | Resolved for product protocol naming; core runtime selection and ESP adapter factory now route legacy `RNode` values to a concrete product-level `ReticulumAdapter` rather than exposing a raw RNode or LXMF user protocol |
| RCNF-002 | Reference coverage | `microReticulum` does not provide a complete LXMF messaging stack | upstream-gap | Use it only for Reticulum network-stack checks |
| RCNF-003 | Address projection | Trail Mate still carries 32-bit node ids as compatibility projections, while V9 node-store persistence, chat message metadata, core in-memory conversation keys, and incoming duplicate detection keep/use full Reticulum destination identity when available | intentional-subset | Continue migrating persistent chat-store grouping, `ReadStateLedger` keys, SD/index projections, and UI presentation conversation ids to carry Reticulum destination identity. Index/header unread mirrors are not authoritative under `RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md` |
| RCNF-004 | MTU model | Trail Mate currently uses a fixed embedded Reticulum MTU constant | intentional-subset | Verify against interface MTU behavior before changing runtime |
| RCNF-005 | Runtime ownership | `LxmfAdapter` still owns LXMF envelope/service orchestration, signature verification, queue policy, link response sending, and propagated delivery acceptance side effects, while transport, link, resource, propagation state, propagation request response planning, and verified packed-payload delivery classification/materialisation are now split into dedicated runtime helpers | missing-feature | Continue splitting remaining LXMF service orchestration after conformance tests expose stable boundaries |
| RCNF-006 | Announce validation | Trail Mate parses and cryptographically validates the `microReticulum` announce vectors in conformance smoke coverage | missing-feature | Resolved for the checked announce-vector subset |
| RCNF-007 | Runtime state ownership | Trail Mate has explicit transport, link, resource, and propagation state objects for the current embedded Reticulum runtime | missing-feature | Resolved for the state ownership contract subset; behavior splitting remains tracked by RCNF-005 |
| RCNF-008 | Supported-subset golden baseline | Trail Mate now has deterministic product-subset golden vectors for hash, destination, packet, LXMF envelope/app-data, link request/response, resource, propagation, and negative parser bytes | intentional-subset | Treat as the current product baseline; keep canonical Python Reticulum/LXMF cross-check as a later conformance upgrade |

## Fixture Rules

Conformance fixtures must be deterministic:

- fixed identity key material
- fixed destination app name and aspects
- fixed payload bytes
- fixed packet context and transport type
- fixed timestamps where the reference path allows injection
- fixed random blobs or recorded raw packets where randomness cannot be injected
- exact reference commit recorded next to generated vectors

Generated packet vectors must include both the raw bytes and the parsed semantic
fields expected by Trail Mate.

## Test Layers

### 1. Golden Vectors

Golden vectors compare byte-level outputs:

- name hash
- identity hash
- destination hash
- packet header bytes
- packet hash
- truncated packet hash
- announce payload layout
- proof payload and proof destination

The first Trail Mate supported-subset vector set is
`tests/reticulum_conformance/fixtures/trailmate_supported_subset_vectors.json`
and the corresponding C++ smoke target is
`trailmate_reticulum_supported_subset_vectors_smoke`. This set is a product
baseline for the subset already needed by Trail Mate business paths:

- Reticulum hashes, destination derivation, packet header bytes, packet hashes,
  and proof packet bytes
- LXMF text payload, Trail Mate app-data payload, message hash, signed part,
  and packed envelope bytes
- link request/response bytes used by propagation service planning
- resource advertisement and hashmap-update bytes
- propagation offer, get, id-list, message-list, and batch bytes
- negative parser rejection for short packets and invalid msgpack

It is marked `exploratory` because it is generated from fixed inputs against
Trail Mate's declared supported subset. It becomes a stronger conformance
artifact only after the same inputs are independently cross-checked against
Python Reticulum and an interoperable LXMF implementation.

### 2. Parser Differential Tests

Parser differential tests feed reference-generated packets into Trail Mate and
assert semantic parsing:

- packet type
- destination type
- transport type
- context
- context flag
- hop count
- transport id
- destination hash
- payload length and payload offset
- announce public key, name hash, random hash, signature, ratchet, and app data

### 3. Behavior Trace Tests

Behavior traces compare externally visible runtime decisions instead of internal
data structures:

- announce received -> path learned
- duplicate packet received -> packet dropped
- path request received -> cached announce response emitted when known
- data packet delivered -> proof emitted when required
- proof received -> packet receipt concluded
- link request sent -> link state progresses or times out
- resource advertised -> requested, transferred, proven, and completed

### 4. Runtime Ownership Contract Tests

Runtime ownership contract tests compile and exercise the embedded runtime state
objects used by the current product adapter:

- transport paths, packet filter, pending path requests, and reverse proof table
- link sessions, pending requests, deferred payloads, and close state
- resource transfer and assembly state
- propagation entries, transients, and peer state

These tests do not replace behavior traces or golden vectors. They prevent the
product runtime from losing explicit ownership boundaries while the monolithic
adapter is being split by responsibility.

### 5. Interop Smoke Tests

Interop tests must eventually cover:

- Trail Mate test node to Python Reticulum node
- Trail Mate test node to `microReticulum` node
- Python Reticulum node to `microReticulum` node as a reference sanity check

The initial smoke-test matrix is announce, path request, single packet delivery,
proof, link establishment, and resource completion.

## Implementation Guardrails

- Do not add `microReticulum` to production PlatformIO or CMake targets during
  conformance-only work.
- Do not change `IMeshAdapter` semantics to mirror Reticulum internals.
- Do not allow conformance fixture convenience to define product terminology.
- Do not let protocol factory code select `LxmfAdapter` or `RNodeAdapter`
  directly for the user-facing Reticulum protocol; enter through the
  product-level Reticulum adapter boundary.
- Do not mark a mismatch as fixed until the deviation class is recorded.
- Do not use `microReticulum` to validate LXMF propagation behavior.
- Do not treat RNode or LXMF as top-level protocols in new UI or config design.

## First Construction Slice

The first branch slice is:

1. Add this baseline.
2. Add a dev-only conformance fixture directory.
3. Record initial deviations.
4. Add byte-level golden vector tests for `reticulum_wire`.
5. Add announce-vector parser/validation smoke coverage.
6. Add runtime ownership contract smoke coverage for transport, link, resource,
   and propagation state objects.
7. Add Trail Mate supported-subset golden vectors for the Reticulum/LXMF bytes
   currently used by product business paths.
8. Grow behavior-trace tests for path request, proof, link, and resource
   behavior.

Runtime replacement or direct `microReticulum` integration is explicitly out of
scope for this first slice.
