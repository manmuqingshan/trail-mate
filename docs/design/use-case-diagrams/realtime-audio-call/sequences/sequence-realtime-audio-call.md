# Sequence Diagram: Answering and resource exclusive

```mermaid
sequenceDiagram
  participant LXST as LXST Call Adapter
  participant Call as ReticulumCall Runtime
 actor U as user
  participant Access as Realtime/Wi-Fi Access
  participant Media as Audio Engine
  LXST->>Call: beginIncomingIdentifying(peer)
  LXST->>Call: markIncomingRinging(link)
  Call->>Access: beginSoftPreempt(link)
  U->>Call: accept()
  Call->>Access: beginExclusive(link)
  LXST->>Call: markLinkActive(link)
  Call->>Media: start()
  Media-->>Call: ready / failed
  alt ready
    Call-->>U: ActiveCall
  else failed
    Call->>Access: end(link)
    Call-->>LXST: close request
  end
```

## Scenarios and participants

LXST Adapter has link callback, Call Runtime has session phase, user provides accept intent, Access Runtime decides soft/exclusive resources, Audio Engine only reports media startup results. No adapter callback can directly set the UI to ActiveCall.

## Allowed event reordering

`accept()` and `markLinkActive()` can exchange the order. Call Runtime records accepted and linkActive respectively, and only completes the convergence once when both exclusive resources and Media ready are satisfied. Repeated callbacks for the same link are idempotent; late callbacks from other links/generations are rejected.

## Resource upgrade

Ringing first `beginSoftPreempt`; accept and then `beginExclusive`. Media will not be started when exclusive access fails, and soft preempt will end. Media ready is the last guard, and the audio queue cannot be opened just because the link is active.

## Closing sequence

Failure or hangup first stops the session from accepting media frames, then stops Audio, releases Access, and finally requests link close. Close request and remote close can compete and are uniformly terminated by the idempotent termination path.

## Testing

Arrange accept/linkActive/mediaReady three events, covering exclusive failure, media failure, remote early closure, multiple links and repeated close.
