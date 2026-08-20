# P2 · [Boundary defect] Team domain events directly rely on Chat to receive metadata

Status: **acknowledged**
Category: **Dependency and Coupling**

## Conclusion

The Team use case interface directly uses `chat::RxMeta`, making the team model rely on the transmission metadata layout of the communication module. What really belongs to Team is "who sent the team command in what authenticated context", not how Chat currently organizes all receive fields.

## Risk

- Adding or removing fields from Chat will force changes to the Team interface.
 - Team may accidentally use a transport field that has not been authenticated by the identity layer.
- The minimal contracts required for authorization, deduplication and auditing cannot be seen and tested.

## Target contract

`TeamReceiveContext` only retains:

- verified peer identity;
- team/protocol namespace;
- receive timestamp;
- message identity / replay protection required fields;
- Specifies the required link quality (if the business rule does use it).

Explicitly mapped by adapter from `chat::RxMeta`. In this way, Chat can evolve its own receiving metadata, and Team only changes when the business contract changes.

Evidence: `modules/core_team/include/team/usecase/team_service.h` and `modules/core_chat/include/chat/domain/chat_types.h`.
