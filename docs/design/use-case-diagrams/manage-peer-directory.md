# Use Case: Management contacts, nearby nodes and local trust

Status: **confirmed; identity linking partial**

Business boundary: network, identity and directory

Main participants: device user
Event triggerer: active protocol backend, Reticulum directory

## User Goals

Understand who the device discovered, save peers worth keeping as contacts, set local names, ignore noisy nodes, check protocol identities, and flag human verification when evidence is available.

## Enter the directory

1. Meshtastic/MeshCore node information or Reticulum LXMF address form a protocol observation first.
2. The directory distinguishes records with `protocol + protocol identity`, retains first seen, and updates last seen, display name, location, capability and public key facts.
3. The Contacts page projects Contacts, Nearby, Reticulum Groups and Ignored according to the active protocol, instead of merging NodeIds with the same number in different protocols.

## User action

- Save as contact and set nickname.
- Edit nickname; deleting a contact only deletes the user relationship, which does not mean deleting the protocol node observation.
- Ignore/unignore nodes.
- Delete node records.
 - View key/hash; set manually verified only if node record exists.
- Enter session from contact or node details; Reticulum group destination uses independent persistent state.

## Failure and recovery

- Persistence failure cannot be updated to "Saved".
- Unknown protocols, empty identities, and all-zero keys do not lead to verifiable identities.
- Similar names across protocols cannot automatically establish the same contact relationship.
- `isNodeVisible()` currently does not perform the six-day filtering stated in the document; the UI should not interpret "nearby" as a fulfilled retention policy.

## Design that has not yet been formed

The revocable `IdentityLink` from the protocol identity to the business contact does not exist; manual verification, Reticulum trusted and Mesh verified key are not in the same certification state.

## Source code evidence

- `modules/core_chat/include/chat/domain/mesh_peer_directory.h`
- `modules/core_chat/include/chat/usecase/contact_service.h`
- `modules/ui_shared/src/ui/screens/contacts/contacts_page_runtime.cpp`
- `modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp`

## Drill down

- [Activity: Observed Contacts](manage-peer-directory/activity.md)
- [Sequence: Protocol Observation, Directory and Contacts Projection](manage-peer-directory/sequences/sequence-manage-peer-directory.md)
- [State Machine: local relationship status of directory entry](manage-peer-directory/state-machines/peer-local-relationship.md)
