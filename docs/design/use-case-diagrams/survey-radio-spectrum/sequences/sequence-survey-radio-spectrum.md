# Sequence: Protocol Probe and Radio Owner
```mermaid
sequenceDiagram
 actor U as user
  participant UI as Protocol Probe
  participant Plan as Candidate Profile Queue
  participant Radio as LoRa Runtime
  participant Adapter as Protocol Adapter
  UI->>Plan: build(protocol-specific finite profiles)
  UI->>Radio: acquire + configure first full profile
  loop each candidate profile
    UI->>Radio: configure_receive(profile)
    Radio-->>UI: CRC-passing raw frame or timeout
    UI->>Adapter: parse(protocol, raw frame)
    alt MeshCore evidence
      UI->>Radio: rate-limited Discover TX
      Radio-->>UI: response or ACK
    else Meshtastic evidence with context
      UI->>Radio: targeted want_ack unicast TX
      Radio-->>UI: correlated ROUTING ACK or timeout
    else Reticulum public control traffic
      UI->>Adapter: validate Announce or fixed-control Path Request
    end
    UI->>UI: retain OBSERVED; upgrade only positive response to CONFIRMED
  end
  U->>UI: select observed/confirmed profile
  UI->>UI: show apply confirmation
  UI->>Radio: release temporary lease before config apply
  UI->>Radio: release on exit
```

## Scenarios and responsibilities

Candidate Profile Queue is responsible for limited, interpretable complete PHY assumptions, Protocol Probe orchestrates evidence and protocol-specific verification, and Radio Runtime owns hardware configuration and reception. The application after the user selects the profile is submitted independently.

## Acquisition and recovery

`acquire` returns the lease and the configuration snapshot before entry; configure is not called if the lease is not acquired. Stop detection before exiting, canceling, error or applying, then restore the radio configuration that needs to be retained and release.

## Sampling order

Configure each item first, wait for the hardware to settle, and then enter RX. The original frame must belong to the current candidate generation, and late frames cannot be counted in the next profile. After the unsolicited packet is sent, the scheduler must stay in the same profile to receive the complete response; unreturned ACK cannot be used as negative evidence. RT does not have an active Probe, nor does it have a Proof waiting window on this page.

## Apply Selection

There is no AUTO/noise/hot selection. Only E2/E3 profiles can trigger Set and must pass the confirmation dialog. The configuration projection is updated only after the persistent mapping of the target protocol is successful; the old configuration is retained if it fails.

## test

 Covers settle, late frames, protocol parsing failure, positive/negative verification of MC/MT, RT passive observation, response window, partial result, no applicable profile, apply failure, resource preemption and post-release configuration.
