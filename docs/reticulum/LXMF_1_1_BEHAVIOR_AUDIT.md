# LXMF 1.1 behavior audit

This audit records the client-side LXMF 1.1 behavior implemented by Trail Mate.
Trail Mate is an embedded LXMF endpoint; propagation-node server duties are not
part of the product role.

| LXMF 1.1 area | Trail Mate behavior | Verification |
| --- | --- | --- |
| Direct and propagated envelope validation | Validates destination, source identity, signature, message hash, timestamp and supported field bounds before commit | supported-subset vectors and runtime-state contract |
| Stamp validation | Supports propagation stamp cost/validation and rejects invalid offers before durable delivery | propagation runtime contract |
| Propagation sync lifecycle | Tracks offer, request, response, completion, retry and terminal failure without publishing a partial message | runtime-state contract |
| Inbound Resource tracking | Tracks advertised size, transfer size, hashes, bitmap, window and split assembly; new 3 MiB, 8192-part, 256-segment and 64-window ceilings are checked before allocation | runtime-state contract plus ESP stack guard |
| Resource cancel semantics | Handles ICL/RCL, exposes user cancel for message and Nomad receives, sends RCL and removes matching local transfer state | ESP product build and runtime tests |
| Nomad Link Request data | Sends `nil` for ordinary reads and a bounded MsgPack map for Micron form submissions | Micron contract/corpus and ESP product build |
| Durable receive commit | Commits only a fully reassembled, decrypted/decompressed, hash-verified LXMF envelope | runtime-state contract |
| Concurrent file safety | Existing atomic cache/ledger writes and storage bus gates remain authoritative | Linux directory runtime smoke and ESP product build |
| Propagation server peer fixes | Not applicable: Trail Mate is a propagation client, not an LXMF propagation node | product-role boundary |

The audit baseline is LXMF 1.1.0 and RNS 1.4.0. Interoperability-sensitive
wire behavior is kept in `tests/reticulum_conformance` rather than inferred from
UI state.
