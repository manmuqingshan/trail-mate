# Domain model that has not yet been formed

Status: **has_unresolved** · 4 findings

The first three are not that Model Explorer missed existing classes, but that business capabilities have emerged and the core owner has not yet been formed. The fourth item is the Projection/Workflow that already has a stable state language in the source code, but still needs to be determined as an independent Model or an existing Model.

- P1: [Route navigation rules are still held by UI Runtime](../issues/route-navigation-domain-model-missing.md)
- P1: [Team members and team lifecycles have no domain owner](../issues/team-membership-lifecycle-model-missing.md)
- P1: [IdentityLink from protocol identity to business contact is missing](../issues/peer-identity-ownership-split.md)
- P2: [System and Media Runtime has not completed Model-or-Projection classification](../issues/runtime-model-candidates-unclassified.md)

The closing criterion is not "add a class name to the document", but that the code appears in a verifiable state owner, invariants, commands/events, and tests, with boundaries confirmed by the author; candidates must also be clearly adjudicated as independent models, existing model elements, application workflows, or integrated projections.
