# Issue #21 T-Deck Meshtastic Fix Checklist

## Problem Description

User Feedback Trail Mate has a phenomenon that "Meshtastic can receive but not send, and the final message determination fails" on the original T-Deck, but the reference firmware in `.tmp/firmware` works normally on the same hardware.

## Root cause summary

- T-Deck's SX1262 initialization does not align with the reference firmware, missing explicit `TCXO 1.8V`, `DIO2 RF switch` and `140mA current limit` alignment.
- When Radio shares SPI with Display, T-Deck board-level code uses a short lock wait time, which may cause `TX -> RX` switching to fail during UI flush.
- `MtAdapter` has multiple direct sending paths that bypass the local `rx_started` state of `radioTask()`, causing RX to be silently lost once `startRadioReceive()` fails.
- `radioTask()` previously only restarted reception after `RX_DONE`, and there was no unified recovery logic for `CRC_ERR`, `HEADER_ERR`, `TIMEOUT` and other terminating IRQs.
- The current ACK logic will only wait for a timeout and then determine failure. It does not retain the sent reliable message for re-transmission. There is a clear gap with the reliable retransmission model of the reference firmware.

## Fix List

- [x] Align SX1262 boot parameters for T-Deck: Explicitly use `TCXO 1.8V`, `DIO2 as RF switch`, `140mA current limit`.
- [x] Change the key Radio SPI access of T-Deck to a blocking lock to avoid being unable to return to the receiving state in time after transmitting due to display refresh preemption.
- [x] Add shared RX status and "request to restart RX" mechanism to `AppTasks`, so that the direct send path and `radioTask()` use the same set of receive recovery states.
- [x] Make `radioTask()` uniformly perform RX restarts after `RX_DONE / CRC_ERR / HEADER_ERR / TIMEOUT` instead of relying on a single local state variable.
- [x] Unify the wireless transmission entrance of `MtAdapter`, all Meshtastic direct paths use the same `transmitWirePacket()` to complete RX recovery after TX.
- [x] Preserve original wire packet for Meshtastic sends that require ACK, and perform up to `3` reliable retransmissions after ACK timeout.
- [x] Correct the target node, channel and `channel_hash` metadata when reporting ACK timeout failure to avoid false reporting as primary channel.
- [x] After the text sending queue reaches the maximum retry, reissue explicit failure results to avoid silent discarding.

## Scope description

- This fix does not transplant the entire `.tmp/firmware` of `ReliableRouter` / `NextHopRouter`, but the three-layer differences most directly related to this problem have been completed:
- Board-level RF initialization alignment.
-Restore link alignment after sending and returning to receiving state.
- Complete the reliably sent ACK timeout retransmission capability.

## Recommended verification

- `pio run -e tdeck`
- `pio run -e tlora_pager_sx1262`
- `pio run -e lilygo_twatch_s3`
- `pio run -e gat562_mesh_evb_pro`
- `clang-format-14 --dry-run --Werror ...` Check the format according to CI rules
