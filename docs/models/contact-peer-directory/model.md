# Contact, peer directory and local trust model

Model status: **confirmed / boundary split**
Authority code: `core_chat`'s `MeshPeerRecord`, `IMeshPeerDirectory`, `ContactService` and their stores

## What does this model answer?

Trail After Mate observes a node from a wireless protocol, it must decide four different things: who it is within the protocol, what facts the system knows about it, whether the user saved it as a contact, and whether the machine ignores or trusts it.

These decisions have become operational models and cannot continue to be buried in the adjunct types of "Chat" or "Mesh Identity". The difference between it and the message model is that the message model has session and delivery states; this model has local directories, user aliases, visibility, ignore and trust projections.

## Core language

| Concept | Code expression | Meaning | Current owner |
| --- | --- | --- | --- |
| Peer identity | `MeshPeerIdentity` | Directory key consisting of protocol and NodeId, public key or Reticulum destination | `MeshPeerRecord` / directory |
| Protocol facts | `MeshtasticPeerFacts`, `MeshCorePeerFacts`, `ReticulumPeerFacts` | Protocol observed names, routes, public keys and capabilities | `MeshPeerRecord` |
| Observation facts | `MeshPeerObservations` | SNR, RSSI, device metrics and last position | `MeshPeerRecord` |
| User facts | `MeshPeerUserFlags`, `user_alias` | favorite, ignored, trusted and user aliases | local directory |
| Node projection | `NodeInfo` / `NodeEntry` | Node representation for legacy contact services and UI | `INodeStore` |
| Contacts | nickname attached to a node id | User actively saved and named nodes | `IContactStore` |

`PeerPublicKey` belongs to Mesh Key identity model; `MeshPeerIdentity` is a directory index. The two are related, but they cannot be regarded as the same entity just because the names are similar.

## The path of the peer into the directory

1. Meshtastic, MeshCore or Reticulum adapter generates protocol observations.
2. Observations are normalized to `MeshPeerIdentity` and protocol facts.
3. `IMeshPeerDirectory::record` inserts or merges `MeshPeerRecord` according to stable identity.
4. The old path updates `NodeEntry` in `INodeStore` through `ContactService::applyNodeUpdate`.
5. The contact page reads contacts, nearby nodes and ignored nodes according to the current active protocol.
6. The user can set nickname, ignored, or manually verified; these are native facts and should not be silently overwritten by subsequent wireless observations.

## Already existing rules

### The identity must be valid

`meshPeerIdentityIsValid` checks three types of directory keys respectively: NodeId cannot be zero, public key must have non-zero bounded bytes, Reticulum destination must be valid and hash is non-zero. A record without a valid identity cannot become a directory fact.

### The protocol partition is the query boundary

`ContactService::setActiveProtocol` switches node store and contact store at the same time, and clears the cache. RNode is normalized to Reticulum. Contact queries should not directly treat NodeIds with the same value in different protocols as the same person.

### The contact and the node are not in the same life cycle

- `removeContact` only deletes the nickname, and the node observation can still be retained.
- `removeNode` attempts to delete nickname and node records at the same time.
- `addContact` / `editContact` will first ensure that the node entry exists; therefore the contact action relies on the directory record, but the contact status is not the wireless observation itself.

### Native trust can only modify existing nodes

`setNodeIgnored` and `setNodeKeyManuallyVerified` return failure when the node entry does not exist. They are written back to the store via `NodeUpdate` and do not create trusted identities out of thin air.

### The display name has a certain downgrade order

Common protocols give priority to nickname, and then use the node name; Reticulum path priority protocol announce/long name, and then use nickname when the protocol name is missing. This difference is existing behavior and should not be re-guessed by the UI individually.

## There is currently no closed place

### Two sets of directory expressions coexist

`MeshPeerRecord / IMeshPeerDirectory` can already express protocol identity, source, first/last observation, user flags and cross-protocol facts; `NodeInfo / NodeEntry / ContactService` still has another set of location, indicator, ignored and verified fields. The platform adapter needs to project between two sets of structures, which can easily lead to double writing and inconsistent coverage order.

### There is no actual freshness threshold for "nearby nodes"

 The interface annotation of `ContactService` says that nearby nodes only retain visibility for six days, but currently `isNodeVisible` returns `true` unconditionally. Although `formatTimeStatus` calculates Online/Seen/Offline, it does not become a query invariant. Therefore the documentation cannot claim that the code has implemented the six-day expiration policy.

### Trusted, manually verified and Mesh verified keys have not been unified yet

There are `MeshPeerUserFlags::trusted`, Meshtastic `key_manually_verified`, MeshCore `public_key_verified` in the directory, and the Mesh key model also has `PeerPublicKey::verified`. There is no single owner of their provenance, revocation, and priority.

### Missing business identity link

Protocol nodes, directory records, contacts, and future team members are still related through NodeId, destination hash, or temporary mapping. The code has no explicit, revocable `IdentityLink`. This gap remains in Review Queue, and this article does not create an existing class.

## Relationship to other models

 - The Mesh identity model provides authenticated key facts; this model determines how peers are indexed, named, and projected in the local directory.
- The communication model references the contact display name but does not own the contact lifecycle.
- The Team model currently uses NodeId to maintain the roster; future memberships should reference this model through explicit links rather than duplicating the contact structure.
- Positioning provides native fix; this model saves the position observations reported by the peer, and the credibility and freshness of the two cannot be mixed.

## Source code evidence

| Evidence | Description |
| --- | --- |
| `modules/core_chat/include/chat/domain/mesh_peer_directory.h` | Identity, records, protocol facts, observations and user flags for the new directory |
| `modules/core_chat/include/chat/ports/i_mesh_peer_directory.h` | record/find/search/flags/remove/flush contract |
| `modules/core_chat/include/chat/domain/contact_types.h` | Old node and contact query projection |
| `modules/core_chat/include/chat/usecase/contact_service.h` | Contact and node directory use case entry |
| `modules/core_chat/src/usecase/contact_service.cpp` | Protocol partitioning, nickname, ignored, verified and delete semantics |
| `modules/core_chat/tests/test_mesh_peer_directory_contract.cpp` | first-seen merge and verified key anti-coverage contract |

## Drill down

- [Peer observation, contact promotion and local trust] (peer-directory-lifecycle.md)
- Review Queue: IdentityLink Missing, double expression of directory, missing team member identity
