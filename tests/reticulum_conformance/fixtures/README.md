# Reticulum Conformance Fixtures

Fixtures in this directory are deterministic reference artifacts used by
dev-only conformance tests.

## Required Metadata

Each fixture set must include a metadata file with:

```text
reference_name:
reference_repo:
reference_commit:
generated_at:
generation_command:
canonicality:
inputs:
expected_fields:
notes:
```

`canonicality` must be one of:

```text
canonical
secondary
exploratory
```

## Fixture Types

| Type | Contents |
| --- | --- |
| `hash-vector` | Input bytes and expected full/truncated hash |
| `destination-vector` | Identity material, app/aspect names, and expected destination hash |
| `packet-vector` | Raw packet bytes and expected parsed packet fields |
| `announce-vector` | Raw announce packet and expected announce fields |
| `proof-vector` | Data packet, proof packet, expected receipt/proof result |
| `trace-vector` | Ordered external events and expected runtime decisions |

## Current Fixture Sets

| File | Reference | Canonicality | Purpose |
| --- | --- | --- | --- |
| `microreticulum_announce_vectors.json` | `attermann/microReticulum` at `c02b6e3d985567cced4450fb65902106ebda5a39` | secondary | Announce packets accepted by `Identity::validate_announce`, including signature and destination-hash checks |
| `trailmate_supported_subset_vectors.json` | Trail Mate fixed-input generator at `649d852ae33c75981216d238e3ab68fd24239b5b` | exploratory | Product-supported Reticulum/LXMF subset bytes for hash, destination, packet, LXMF envelope, link request/response, resource, propagation, and negative parser cases |

## Rules

- Do not store private production identities.
- Do not store floating reference names such as `main` without a commit hash.
- Do not treat `microReticulum` vectors as canonical when Python Reticulum
  produces a different result.
- Do not add product-specific Team or UI behavior to Reticulum network fixtures.
- Do not treat Trail Mate supported-subset vectors as proof of complete
  Reticulum conformance; they lock the product subset until canonical
  cross-reference vectors are added.
