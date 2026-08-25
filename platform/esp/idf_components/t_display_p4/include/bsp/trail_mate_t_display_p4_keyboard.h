#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Starts the T-Display-P4 K270 keyboard adapter.
 *
 * The adapter owns only the P2 (GPIO46/GPIO45) XL9555/TCA8418 keyboard
 * chain. It polls the controller FIFO from an LVGL timer and routes pressed
 * keys directly to the active P4 UI; it deliberately does not create an LVGL
 * keypad input device or follow the mutable default focus group.
 *
 * A missing expansion keyboard is optional and returns false.
 */
bool trail_mate_t_display_p4_keyboard_start(void);

/** True only after both P2 controllers have been initialized. */
bool trail_mate_t_display_p4_keyboard_is_ready(void);

#ifdef __cplusplus
}
#endif
