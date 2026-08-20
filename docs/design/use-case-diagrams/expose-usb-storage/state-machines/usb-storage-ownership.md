# State Machine:USB Storage Ownership
```mermaid
stateDiagram-v2
  [*] --> ApplicationOwned
  ApplicationOwned --> Preparing: enter USB
  Preparing --> HostOwned: owners quiesced + SD unmounted + MSC started
  Preparing --> Restoring: any failure
  HostOwned --> Restoring: exit/disconnect
  Restoring --> ApplicationOwned: MSC stopped + SD remounted + owners resumed
  Restoring --> Error: remount/restore failed
  Error --> Restoring: retry recovery
```

## Status owner

The USB Support Runtime is the sole coordinating owner and holds the session generation, paused owner collection, and media stages. Application SD Host and MSC backend only report their own operation results.

## Ownership invariants

When ApplicationOwned, the application can access the SD and the MSC must be stopped; when HostOwned, the MSC can be accessed, the application must be unmounted and all related owners are quiescent. Preparing/Restoring is a transition state that cannot be declared externally as writable.

## Transition table

| Current state | Completion guard | Next state |
| --- | --- | --- |
| ApplicationOwned | enter accepted | Preparing |
| Preparing | all quiesced + unmounted + MSC active | HostOwned |
| Preparing | Failure in any stage | Restoring |
| HostOwned | exit/disconnect/error | Restoring |
| Restoring | MSC stopped + remounted + owners resumed | ApplicationOwned |
| Restoring | remount/resume failed | Error |

## Ban and restore

The second enter is rejected in Preparing/Restoring/Error. HostOwned does not allow application file operations. Error remains affected owner paused until retry recovery is clearly successful; cannot pretend to be ApplicationOwned in order to return to the UI homepage.

## test

Injection failures for each intermediate stage, verifying reversal compensation, ownership mutual exclusion, repeated exits, and media checks after restarts.
