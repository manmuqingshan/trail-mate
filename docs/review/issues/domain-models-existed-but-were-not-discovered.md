# P1 · [Fixed discovered bug] Fixed three templates once obscuring the real model

Status: **resolved**
Category: **Discovered/Document Indexing Bug**

## Conclusion

Trail Mate does not only have three Models. The old Registry hard-coded "organization and process, software structure, deployment and artifacts" into top-level types, and then stuffed the real domain facts into these containers. The current author Registry has removed this restriction and lists nine model boundaries based on the source code.

## Direct evidence

- `ChatMessageLedger` has message delivery state and idempotent merging.
- `PeerIdentityService` has local identity establishment and peer public key protection rules.
- `TeamPairingCoordinator` owns the team pairing process.
- `LocationService` has fix validity and time authority.
- `TrackStateMachine` has a complete track record command and status.
- `TargetManifestView`, `CapabilityStatus`, `AuthorityBinding` form the device capability language.
- `SessionRuntime`, `LinkState` and frame router form cross-processor session boundaries.
- `IPhoneAppFacade` forms the mobile phone interoperability boundary with two protocol cores.

## Why is P1?

Model Explorer is the entrance to the architecture review. Presenting model discovery failure as "the project has no model" will directly distort design judgment and induce maintainers to continue designing on the error boundary.

## Repair Guidelines

1. The Registry accepts any number and type of models declared by the author.
2. Model must list owner, invariants, source code evidence and cross-model relationships.
3. Discovery failure is displayed as finding, and an empty Model is not automatically generated.
4. Design, Engineering and C4 are only used as projections.

The current author Registry has listed nine models according to these guidelines. In the future, each specific leak identification model must form a separate finding; this historical question can no longer be used to replace the integrity audit.
