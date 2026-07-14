# Reticulum Conformance Deviations

This ledger records mismatches between Trail Mate, Python Reticulum, and
`microReticulum`.

Each entry must be classified before it drives code changes.

## Classes

| Class | Meaning |
| --- | --- |
| `bug` | Trail Mate contradicts supported Reticulum behavior |
| `intentional-subset` | Trail Mate deliberately supports a smaller embedded subset |
| `missing-feature` | Valid Reticulum behavior outside the current implementation |
| `product-extension` | Trail Mate behavior above Reticulum protocol scope |
| `reference-disagreement` | Python Reticulum and `microReticulum` disagree |
| `upstream-gap` | The selected reference does not cover the behavior |

## Open Deviations

| ID | Area | Observation | Class | Next step |
| --- | --- | --- | --- | --- |
| RCNF-002 | LXMF coverage | `microReticulum` does not provide a complete LXMF messaging/propagation implementation | upstream-gap | Use Python LXMF or compatible clients for LXMF-specific checks |
| RCNF-003 | Reticulum address projection | Current product model still keeps 32-bit `NodeId` values as compatibility projections, while V9 node-store persistence, chat message metadata, core in-memory conversation keys, and incoming duplicate detection now retain/use full Reticulum destination identity when available | intentional-subset | Continue migrating persistent chat-store grouping, unread keys, SD/index paths, and UI presentation conversation ids to carry Reticulum destination identity |
| RCNF-004 | MTU model | Trail Mate Reticulum helpers use a fixed embedded MTU constant | intentional-subset | Compare with reference interface MTU behavior before runtime changes |
| RCNF-005 | Runtime responsibility | `LxmfAdapter` still orchestrates LXMF envelope unpacking, signature verification, queue push/drop policy, link response sending, and propagated delivery decryption, but transport table operations live in `lxmf_transport_runtime.*`, link session lifecycle rules live in `lxmf_link_runtime.*`, resource bookkeeping/window/proof/assembly/TTL rules live in `lxmf_resource_runtime.*`, propagation entry/transient/peer bookkeeping lives in `lxmf_propagation_runtime.*`, propagation `/offer`/`/get` response planning, batch acceptance, remote peer bookkeeping, and propagated message acceptance decisions live in `lxmf_propagation_service_runtime.*`, and verified LXMF packed-payload classification plus text/app-data delivery materialisation lives in `lxmf_delivery_runtime.*` with smoke coverage | missing-feature | Continue splitting remaining LXMF service orchestration after conformance tests define stable boundaries for each runtime responsibility |

## Resolved Deviations

| ID | Area | Resolution |
| --- | --- | --- |
| RCNF-001 | Protocol selector | User-facing protocol choices are now Meshtastic, MeshCore, and Reticulum. Legacy raw `RNode` and `LXMF` values are compatibility/internal implementation details. |
| RCNF-006 | Announce validation | The conformance smoke now verifies `microReticulum` announce vectors structurally and cryptographically, including signature validation and destination-hash validation. |
| RCNF-007 | Runtime state ownership | Trail Mate has explicit transport, link, resource, propagation, propagation-service, and delivery runtime helpers for the current embedded Reticulum runtime ownership contract. |
| RCNF-008 | Supported-subset golden baseline | Trail Mate has deterministic product-subset golden vectors for the Reticulum/LXMF bytes currently used by supported business paths; canonical Python Reticulum/LXMF cross-check remains a later conformance upgrade. |
| RCNF-009 | Wi-Fi gateway final LinkRequest delivery | A Header 2 LinkRequest addressed to a local destination is now accepted as final delivery when it arrives from the configured Wi-Fi gateway, even when the retained transport id belongs to that gateway. Transport-id matching remains required for non-local forwarding and non-gateway ingress. |
