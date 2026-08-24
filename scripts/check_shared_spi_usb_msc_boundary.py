#!/usr/bin/env python3
"""Prevent USB MSC callbacks from widening a shared-SPI transaction."""

from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent
USB_RUNTIME = REPO_ROOT / "platform/esp/arduino_common/src/platform_ui_usb_support_runtime.cpp"
SD_RUNTIME = REPO_ROOT / "platform/esp/arduino_common/src/storage/sd_card_runtime.cpp"


def section(source: str, start: str, end: str) -> str:
    start_at = source.find(start)
    end_at = source.find(end, start_at)
    if start_at < 0 or end_at < 0:
        raise ValueError(f"could not locate section from {start} to {end}")
    return source[start_at:end_at]


def require(text: str, fragment: str, message: str) -> None:
    if fragment not in text:
        raise ValueError(message)


def forbid(text: str, fragment: str, message: str) -> None:
    if fragment in text:
        raise ValueError(message)


def require_bool_call(text: str, argument: bool, message: str) -> None:
    value = "true" if argument else "false"
    pattern = rf"sd_set_external_block_owner_active\s*\(\s*{value}\s*\)"
    if re.search(pattern, text) is None:
        raise ValueError(message)


def main() -> int:
    try:
        usb_source = USB_RUNTIME.read_text(encoding="utf-8")
        sd_source = SD_RUNTIME.read_text(encoding="utf-8")
        read_callback = section(
            usb_source,
            "int32_t usbReadCallback(",
            "int32_t usbWriteCallback(",
        )
        write_callback = section(
            usb_source,
            "int32_t usbWriteCallback(",
            "bool usbStartStopCallback(",
        )
        storage_session_begin = section(
            usb_source,
            "bool begin()",
            "bool end()",
        )
        storage_session_end = section(
            usb_source,
            "bool end()",
            "bool active() const",
        )

        for fragment in (
            "UsbMscBusGate",
            "SharedSpiCoordinator",
            "BusAcquireRequest",
            "BusAccessToken",
            "ScopedBusAccessToken",
            "shared_spi_coordinator()",
        ):
            forbid(
                usb_source,
                fragment,
                f"USB MSC must not acquire shared SPI directly: {fragment}",
            )

        require_bool_call(
            storage_session_begin,
            True,
            "USB MSC must retain its semantic external-owner state",
        )
        require_bool_call(
            storage_session_end,
            False,
            "USB MSC must release its semantic external-owner state",
        )
        require(
            read_callback,
            "storage::sd_read_raw(",
            "USB MSC reads must delegate every sector to the SD runtime",
        )
        require(
            write_callback,
            "storage::sd_write_raw(",
            "USB MSC writes must delegate every sector to the SD runtime",
        )
        require(
            usb_source,
            "uint8_t s_usb_msc_sector_scratch[512];",
            "USB MSC partial-sector work must use bounded static scratch storage",
        )
        for raw_operation in ("bool sd_read_raw(", "bool sd_write_raw("):
            raw_section = section(sd_source, raw_operation, "} // namespace")
            require(
                raw_section,
                "SdRuntimeOperationGuard",
                f"{raw_operation} must stay inside the SD runtime logical guard",
            )
            require(
                raw_section,
                "s_sdfat.card()->",
                f"{raw_operation} must use the SdFat driver physical boundary",
            )
    except (OSError, ValueError) as error:
        print(f"Shared SPI USB MSC boundary check failed: {error}", file=sys.stderr)
        return 1

    print("Shared SPI USB MSC boundary check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
