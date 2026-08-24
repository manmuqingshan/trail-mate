# Sequence: SSTV decoding and saving
```mermaid
sequenceDiagram
 actor U as user
  participant UI as SSTV Page
  participant Runtime as SSTV Runtime
  participant Decoder as Audio Decoder
  participant Store as Image Store
  U->>UI: Start RX
  UI->>Runtime: start_receive
  Runtime->>Decoder: consume audio
  loop signal updates
    Decoder-->>UI: mode, level, progress
  end
  Decoder-->>Runtime: complete frame
  Runtime->>Store: save frame
  Store-->>Runtime: path / error
  Runtime-->>UI: image + save outcome
```

## Scenarios and responsibilities

UI manages an RX session; Runtime has a receiving life cycle and frame buffer; Decoder consumes audio and generates mode/progress/complete frames; Image Store is only responsible for persisting complete images.

## Sequence of events

Progress can only be updated in the current session and the pattern is recognized. `complete frame` is a precondition for saving; Runtime freezes frame ownership before transferring it to the Store to prevent the Decoder from immediately reusing the buffer and causing changes in the saved content.

## Save and display

Store returns path to indicate that the file has been stably submitted. Saving errors does not negate complete decoding. The UI can display the image but clearly indicates that it is not saved; it is only displayed when the path belongs to the current frame, and the last path cannot be used.

## Cancellation, lateness and memory

If the user cancels or exits the incremental session generation, all late progress/complete/save callbacks will be invalid. Frame buffer uses members/fixed slots or caller storage, disallowing the construction of large image objects on the audio task stack.

## Tests

 Covers progress out-of-order, continuous frames, next frame during saving, Store failure, late completion after cancellation and buffer reuse.
