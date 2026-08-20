# Use Case: Detect protocol air interface parameters and confirm availability

Status: **confirmed**
Business Boundary: Communication, Media and Delivery / Radio Tools

## User goal

From a limited, interpretable and complete set of LoRa air profiles, discover MeshCore,
Meshtastic or Reticulum The parameters of the protocol packet and complete active confirmation when the protocol semantics allow.

## Main scene

1. Create a limited candidate queue from the complete profile of the current configuration: Meshtastic joins the standard modem profile of the same configuration context, MeshCore joins the regional preset of the same frequency family, and Reticulum only retains the current RNode profile; no full-band 25 kHz RSSI bins are created, and no historical candidates are forged.
2. Protocol Probe obtains the radio runtime, configures the complete receive profile according to the candidate, and collects the LoRa frames that pass the CRC.
3. Protocol parsing divides the evidence into LoRa frame, protocol observed and active confirmation; only the last two levels enter the result list.
4. MeshCore sends a restricted Discover and waits for the response carrying this tag; Meshtastic must first decrypt and verify a local channel data packet, and then unicast `want_ack` to the source; Reticulum only passively accepts self-consistent Announce or Path Request for fixed control purposes, and does not run Path/Ping/Proof on temporarily tuned profiles.
5. After the user selects the profile that has been observed or confirmed, it must be confirmed twice before it can be applied; leave the page and restore the radio owner.

Failure: Stop probing and display the reason when radio is not supported, configuration fails, or ownership is not available; silent candidates, unresponsive ACKs, or timeout responses cannot be treated as profile non-existence.

Source code: `modules/ui_shared/src/ui/screens/energy_sweep/energy_sweep_page_runtime.cpp`, `modules/core_sys/include/platform/ui/lora_runtime.h`.

## Drill down

- [Activity](survey-radio-spectrum/activity.md)
- [Sequence](survey-radio-spectrum/sequences/sequence-survey-radio-spectrum.md)

This file retains the `survey-radio-spectrum` path only for document link compatibility; its content and
product name have been changed to Protocol Probe.
