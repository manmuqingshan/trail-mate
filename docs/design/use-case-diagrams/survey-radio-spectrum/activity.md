# Activity: Protocol air interface parameter detection
```mermaid
flowchart TD
 Enter --> Plan["Construct limited complete profile queue according to protocol"]
 Plan --> Acquire{"Get radio?"}
 Acquire -- No --> Error
 Acquire -- Yes --> Receive["Configure complete PHY profile + receive"]
  Receive --> Frame{"CRC frame?"}
 Frame -- No --> More
 Frame -- Yes --> Parse{"target protocol parse?"}
 Parse -- No --> More
 Parse -- Yes --> Observe["record OBSERVED evidence"]
  Observe --> Route{"protocol route"}
  Route -- MeshCore --> Mc["Discover -> response/ACK"]
  Route -- Meshtastic --> Mt["known node + channel context -> unicast want_ack"]
 Route -- Reticulum --> Rt["Passive acceptance of self-consistent Announce / control Path Request"]
  Mc --> Confirm["record CONFIRMED when valid"]
  Mt --> Confirm
  Rt --> More
  Confirm --> More{"more candidates?"}
 More -- Yes --> Receive
 More -- No --> Choice{"select observed/confirmed profile?"}
 Choice -- Yes --> Apply["confirm then persist supported profile"]
 Choice -- No --> Release["stop and release radio"]
 Apply --> Release["Exit and release radio"]
```

## Questions answered by this picture

How the system obtains the radio, traverses the complete protocol air profile, records evidence according to the protocol semantics, and only delivers the truly observed or confirmed profile to the user application.

## Scanning plan

 Candidates are derived from the current actual profile, but do not retain unimplemented history cache: Meshtastic only joins the standard modem profile of the same configuration context, MeshCore only joins the area preset of the same frequency family, and Reticulum only listens to the currently configured RNode profile. Each item contains complete PHY parameters such as frequency, BW, SF, CR, sync word, preamble, header/CRC; RT does not assume the existence of a common area plan, nor does it blindly scan the frequency grid.

## Radio Ownership

 Detection requires a temporary exclusive or policy-governed radio lease. If the acquisition fails, the current protocol configuration will not be changed. After active Discover, ACK or Ping, you must keep receiving until the end of the corresponding response window, and cannot retune immediately after sending. Both exit and exception restore the radio configuration before entry and release the lease.

## Evidence rules

E0 RF activity and E1 generic LoRa frame can only enter the diagnosis, not the applicable list. E2 protocol observed is MC's trusted package, MT's decryptable valid data, or RT's self-consistent public Announce/control Path Request; E3 confirmed only comes from the MC Discover response with this tag or the related MT ROUTING ACK. RT does not have an E3 in this exclusive temporary tuning process. No packet or no ACK are uncertain results, and E2 cannot be reversely negated.

## Abort and partial results

Stop new detection steps when the user cancels or the resource is withdrawn, and the completed evidence is retained and clearly identified. Observed/confirmed profiles can be viewed; only when the target protocol has a lossless configuration mapping, the user is allowed to confirm the application again.

## Tests

The verification should cover the current actual profile priority, MC Discover success/timeout, MT broadcast is not misjudged as ACK, MT key is missing, RT Announce/control Path Request passive evidence, radio does not retune in advance within the response window, cancellation, application failure and radio configuration recovery. RT Proof upgrades or arbitrary CRC frames must not be written as protocol proof.
