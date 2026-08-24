# Use Case: Establish team credentials and member relationships

Status: **candidate; pairing behavior confirmed, member model not closed**
Business boundary: team collaboration

## User goal

Create a key-protected team, invite or join the peer, clarify who is the leader/member, and be able to request/distribute keys, remove members or transfer the leader, without treating unconfirmed pairs as permanent members.

## Implemented behavior

1. The leader creates/restores TeamId and TeamKeys, starts pairing and generates pairing messages.
2. The candidate member receives the request, exchanges confirmation/key material after the user confirms.
3. After the key distribution is successful, the UI store restores team mode; `TeamService` remembers the NodeId roster.
4. Key request, kick, leader transfer and status are sent through independent Team payload and the page status is updated by reducer.
5. kick self will clear the local team membership; leader transfer will change subsequent permission projections.

## Invariants and failure

- Unacknowledged or timed out without establishing permanent member.
- Team key decryption/verification failure does not change the roster.
- leader-only action verifies roles before sending.
 - The current roster is mainly `vector<NodeId>`, without membership revision, origin, revocation proof and stable cross-protocol IdentityLink; therefore the full member life cycle is still a candidate.

Source code: `modules/core_team/src/usecase/team_pairing_coordinator.cpp`, `modules/core_team/src/usecase/team_service.cpp`, `modules/ui_shared/src/ui/screens/team/team_page_event_reducer.cpp`.

## Drill down

- [Activity: Pairing to member state](manage-team-sharing/activity.md)
- [Sequence: Leader and Candidate pairing](manage-team-sharing/sequences/sequence-manage-team-sharing.md)
- [State Machine: Pairing Projection with local members](manage-team-sharing/state-machines/team-membership.md)
