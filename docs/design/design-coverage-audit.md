# Design Explorer coverage audit

Review date: 2026-07-23

Conclusion: The old map only has 7 entries, which not only misses a large number of user goals, but also incorrectly merges the goals of different status owners; multiple UMLs are common templates without source code semantics.

## Four types of problems with old maps

1. **Insufficient coverage**: Contacts, Live Calls, GNSS Diagnostics, Spectrum Scan, Walkie, SSTV, Wi-Fi, Package, Firmware, Backup, USB and Phone BLE are not included in the map.
2. **Error merge**: Track recording and route following, team member establishment and team situation sharing belong to different rules and failure boundaries respectively.
3. **Initiator error**: Receiving messages is initiated by the radio/backend event, not by calling UseCase after the user clicks on the UI.
4. **Template impersonation design**: `User → Presentation → Domain Use Case → Port → Platform` does not explain the actual participants, submission points, rollback, resource arbitration and final state rules.

## New Map Judgment

- 21 use cases come from targetable product behaviors, not from menu quantities.
- All use cases must state the goal, trigger, preconditions, success commitment, failure/recovery, rules and source code evidence.
- The existence of a state machine is not equivalent to an independent domain model; Call, Package, Firmware and Wi-Fi Lease are still determined by the model reviewer.
- Route following and team members establish retention candidates to avoid writing missing designs as confirmed.

## Non-use case filtering

Shutdown confirmation, return key, page polling, pure display settings, test helper and platform startup steps are not promoted to top-level business use cases. They can appear in the corresponding document as interaction rules or implementation steps.
