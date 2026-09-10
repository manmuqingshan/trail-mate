# Compact touch editor integration test

This target runs the production editor, IME, page profile and pinyin composition
engine against real LVGL 9.4. It substitutes only language/font resources and
physical keyboard discovery. The pinyin dictionary has two deterministic
candidates; it is a test language-pack fixture.

After installing the wio_tracker_l2 PlatformIO dependencies:

    cmake -S modules/ui_lvgl_ux_packs/tests/touch_editor -B .codex-build/wio-touch -DLVGL_SOURCE_DIR="$PWD/.pio/libdeps/wio_tracker_l2/lvgl"
    cmake --build .codex-build/wio-touch --config Debug --parallel 4
    ctest --test-dir .codex-build/wio-touch -C Debug --output-on-failure

Checks include lower/upper case, punctuation, repeat editing, confirmation,
cancellation, field length limits, pinyin candidates, 320 x 240 layout bounds,
source deletion and owner teardown. The file wio-keyboard.ppm records a
software-rendered keyboard frame in the test working directory for inspection.
