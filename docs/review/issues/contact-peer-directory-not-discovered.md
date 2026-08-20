# P1 · [Existing model not found] Contacts, peer directories and local trusts are not entered into the Model Explorer

Status: **acknowledged**
Category: **Flaws found/Document index**

## Conclusion

The contact directory is not a future idea. `MeshPeerRecord`, `IMeshPeerDirectory`, `ContactService`, `INodeStore` and `IContactStore` already share peer indexing, protocol facts, user aliases, contact categories, ignored and native trust projections. Previous eight-model audits missed this boundary.

This is "the model exists but the tool/author did not find it", not that the product code is missing the model. The fix is ​​to add the real boundaries to the Registry instead of creating another empty class as suggested in the Review Queue.

## Direct Evidence

 - `mesh_peer_directory.h` defines valid directory identities, cross-protocol facts, observations and user flags.
- `i_mesh_peer_directory.h` defines the record/find/search/setUserFlags/remove/flush contract.
- `ContactService` manages protocol partitioning, nickname, near/ignore classification, human validation and delete semantics.
- `IContactStore` and `INodeStore` persist contact nicknames and node observations separately.
- Contacts, key verification, chat projections and multiple platform runtimes all consume this service.

## Documentation fixes performed

 - Added `model:contact-peer-directory`.
- Added 5 new real Element and directory life cycle diagrams.
- Added Mesh Identity → Directory → Conversation / Team cross-model relationship.
- Retain unresolved issues such as the coexistence of old and new directory expressions, missing IdentityLink and invalid visibility policies.

## Acceptance

M
