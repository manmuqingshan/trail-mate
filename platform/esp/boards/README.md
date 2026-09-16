# `platform/esp/boards`

Shared ESP board runtime glue, dispatcher support, and reusable ESP-facing code.

Current responsibilities:

- ESP-specific runtime dispatch under `include/platform/esp/boards/*` and `src/board_runtime.cpp`
- ESP display abstractions and panel drivers under `include/display/*` and `src/display/*`
- board-local input drivers such as rotary handling under `include/input/*` and `src/input/*`
- any remaining ESP-only shared board implementations under `src/board/*`

Boundary note:

- existing device implementations under `boards/<name>/*` retain their contained locations; their presence is not a rule for new targets
- following `docs/specification/POST_REFACTOR_ARCHITECTURE_FREEZE.md`, new board facts belong under `boards/<name>/*`, while new SDK objects, driver execution and scheduling belong in explicit platform adapter packages (for example `platform/esp/wio_tracker_l2`)
- platform-neutral board contracts now live under `platform/shared/include/board/*`
- code that is intentionally shared across multiple ESP boards but still platform-specific belongs here
