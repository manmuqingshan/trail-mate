# ESP UI Lifecycle Spec

Trail Mate ESP targets share one user-visible UI lifecycle. Board profiles may
adapt hardware operations such as backlight, keyboard light, touch IRQ, and
display sleep, but they must not redefine which product page appears for boot
or wake.

## Boot

- Normal startup must show the Trail Mate boot UI before the main menu shell is
  marked ready.
- The boot UI synchronous present policy is shared by pager, TDeck, and
  T-Display style ESP targets. It must not be disabled with board-specific
  preprocessor branches.
- Target startup code may choose the initial boot log text, but it must not skip
  the boot screen only because the target is Arduino, ESP-IDF, TDeck, pager, or
  T-Display.

## Screen Sleep And Wake

- Screen sleep has two separate meanings:
  - physical display sleep, owned by the platform or board runtime;
  - transient screen-saver visibility, owned by shared UI.
- Waking from physical screen sleep must show the shared screen-saver overlay
  first on pager, TDeck, and T-Display style ESP targets.
- Wake input must not directly enter the main menu unless it is explicitly the
  product's "enter from saver" action.
- `enterFromScreenSaver()` is the boundary for leaving the transient saver and
  returning to the normal UI.
- Platform screen-sleep runtimes must not create board-local LVGL screen-saver
  pages, labels, or styles. They may only call the shared screen-saver hooks and
  manage hardware brightness/sleep state.

## Threading

- Non-LVGL tasks must not call LVGL screen-saver or menu functions directly.
  They must post through the platform UI dispatcher or call platform hooks that
  are known to run on the LVGL task.
- Shared screen-saver rendering belongs in `ui_shared`; platform runtimes may
  request an immediate present only when they are already in a LVGL-safe input
  path.

