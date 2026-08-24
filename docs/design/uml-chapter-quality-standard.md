# Design Explorer UML Chapter Quality Standard

This standard governs the drill-down chapters of the Trail Mate Design Explorer. The UML diagram is part of a design argument, not a complete chapter. Any drill-down page must allow reviewers to answer without looking back at the implementation code: This diagram describes which scenario, who owns the state, where the rules take effect, when the commit is completed, how to recover after failure, and which conclusions are only candidate designs.

## Common content of all UML chapters

Each chapter contains at least:

1. **Questions answered by this picture**: limiting scenarios, start conditions and end conditions.
2. **Participants and Responsibilities**: Explain the decision or status owned by each participant, avoid listing only the component name.
3. **UML diagram**: The messages, nodes and states in the diagram must be explained in the text.
4. **Key rules or decision table**: List branch conditions, priorities, guard conditions and invariants.
5. **Complete and Submit Semantics**: Distinguish between request accepted, device operation completed, status persisted and user-visible projection.
6. **Failure, recovery and concurrency**: Explain timeouts, repeated events, resource conflicts, partial success and reentrancy.
7. **Source code evidence and design judgment**: Indicate the current owner; non-existing models must be marked as candidates and cannot be written as implemented facts.

## Activity Diagram

The Activity chapter must be explained additionally:

- Trigger input and its verification;
- Entry conditions and output of each main branch;
- Which steps can be rolled back and which steps form irreversible side effects;
- Whether resource acquisition and release cover each exit path.

## Sequence Diagram

The Sequence chapter must be additionally explained:

-The difference between synchronous calls, asynchronous events and persistent submissions;
- Message order, events that allow out-of-order and idempotent keys;
- Who decides the final state when timeout, ACK, cancellation, retry or disconnection compete;
- When the UI projection can change, the transient state cannot be regarded as the final fact.

## State Machine

The State Machine chapter must have additional explanations:

- Who holds the state and whether it is persistent;
- The events, guards, actions and failure destinations of each transition;
- forbidden transition;
- recovery state after restart, repeated events and abnormal exit.

## Class Collaboration / Composite Structure

The structural class chapter must be additionally explained:

- component responsibilities and dependency direction;
- input and output ports and their contracts;
- owner of shared state; content must not be shared across boundaries;
- test seams and invariants that must be maintained when replacing an adapter or protocol implementation.
