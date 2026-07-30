#!/usr/bin/env python3
"""Check the display-first Arduino startup contract."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parent.parent
STARTUP = ROOT / "apps/esp32_lvgl/src/esp32_lvgl_arduino_startup_runtime.cpp"


def main() -> int:
    text = STARTUP.read_text(encoding="utf-8")
    required = (
        "platform::esp::boards::initializeBoardDisplayHardware(",
        "platform::esp::arduino_common::display_runtime::initialize();",
        "ui::startup_shell::beginBootUi(",
        "platform::esp::boards::initializeBoardServices(",
        "platform::esp::boards::initializeStorage();",
        "arduino_app_runtime_access::initialize(",
    )
    missing = [item for item in required if item not in text]
    if missing:
        print("Boot display order contract failed; missing:")
        for item in missing:
            print(f"- {item}")
        return 1

    positions = [text.index(item) for item in required]
    if positions != sorted(positions):
        print("Boot display order contract failed; phase order is invalid.")
        return 1

    if "startup_support::initializeBoard(waking_from_sleep)" in text:
        print("Boot display order contract failed; legacy full board init remains.")
        return 1

    print("Boot display order contract passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
