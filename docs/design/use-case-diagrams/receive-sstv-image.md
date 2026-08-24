# Use Case: Receive and save SSTV images

Status: **confirmed receive-only behavior**
Business Boundary: Communications, Media and Delivery

## User Goals

Start SSTV reception, observe audio levels, modes and decoding progress, view the image and its save path after the complete image arrives.

## Behavior and Rules

1. Display SSTV when device capability and storage conditions permit.
2. The user starts RX; the runtime enters Receiving from Listening, and continuously reports mode, audio level and progress.
3. Project the image after frame ready; display `last_saved_path` after successful saving.
4. Cancellation, decoding failure or storage failure will end respectively, and the incomplete frame will not be marked as saved.
5. The current UI is RX only and should not claim to support SSTV sending in Design Explorer.

Source code: `modules/ui_shared/src/ui/screens/sstv/sstv_page_runtime.cpp`, `modules/core_sys/include/platform/ui/sstv_runtime.h`.

#

- [Activity](receive-sstv-image/activity.md)
- [Sequence](receive-sstv-image/sequences/sequence-receive-sstv-image.md)
