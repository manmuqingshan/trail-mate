#!/usr/bin/env python3
"""Prevent LVGL's SD filesystem adapter from widening a physical SPI lease.

The adapter may perform semantic filesystem work only. SdRuntimeFile and
SdRuntimeDir own the logical operation scope, while the runtime SdFat driver
hook acquires the shared SPI coordinator at each physical transaction.
"""

from pathlib import Path
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent
LV_HELPER = REPO_ROOT / "platform/esp/arduino_common/src/LV_Helper_v9.cpp"


def adapter_section(source: str) -> str:
    start = source.find("bool sd_fs_ready_cb")
    end = source.find("bool flash_fs_ready_for_io", start)
    if start < 0 or end < 0:
        raise ValueError("could not locate the LVGL SD filesystem adapter")
    return source[start:end]


def require(section: str, fragment: str) -> None:
    if fragment not in section:
        raise ValueError(f"missing required LVGL SD filesystem adapter fragment: {fragment}")


def forbid(section: str, fragment: str) -> None:
    if fragment in section:
        raise ValueError(
            "LVGL SD filesystem adapter must not acquire a shared-SPI token directly: "
            f"{fragment}"
        )


def main() -> int:
    try:
        section = adapter_section(LV_HELPER.read_text(encoding="utf-8"))

        for fragment in (
            "LvglSdBusGuard",
            "SharedSpiCoordinator",
            "BusAcquireRequest",
            "BusAccessToken",
            "shared_spi_coordinator()",
        ):
            forbid(section, fragment)

        for fragment in (
            "SdRuntimeFile",
            "file->open(",
            "file->close()",
            "file->read(",
            "file->write(",
            "file->seek(",
            "file->position()",
            "SdRuntimeDir",
            "dir->open(",
            "dir->read_next(",
            "dir->close()",
            "mark_external_font_fs_busy()",
        ):
            require(section, fragment)
    except (OSError, ValueError) as error:
        print(f"Shared SPI LVGL filesystem boundary check failed: {error}", file=sys.stderr)
        return 1

    print("Shared SPI LVGL filesystem boundary check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
