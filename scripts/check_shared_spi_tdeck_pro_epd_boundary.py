#!/usr/bin/env python3
"""Keep T-Deck Pro EPD page rasterization outside the shared-SPI lease."""

from pathlib import Path
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE = REPO_ROOT / "boards/tdeck_pro/src/tdeck_pro_board.cpp"


def section(source: str, start: str, end: str) -> str:
    start_at = source.find(start)
    end_at = source.find(end, start_at)
    if start_at < 0 or end_at < 0:
        raise ValueError(f"could not locate section from {start} to {end}")
    return source[start_at:end_at]


def require(text: str, fragment: str, message: str) -> None:
    if fragment not in text:
        raise ValueError(message)


def require_before(text: str, first: str, second: str, message: str) -> None:
    if text.find(first) < 0 or text.find(second) < 0 or text.find(first) >= text.find(second):
        raise ValueError(message)


def main() -> int:
    try:
        source = SOURCE.read_text(encoding="utf-8")
        init = section(
            source,
            "bool TDeckProBoard::initDisplay()",
            "bool TDeckProBoard::initTouch()",
        )
        render = section(
            source,
            "DisplayTransferResult TDeckProBoard::renderEpd(",
            "DisplayTransferResult TDeckProBoard::servicePendingEpd(",
        )

        require_before(
            init,
            "epd_.init(0, true, 2, false);",
            "sharedSpiUnlock();",
            "EPD initialization must release its physical SPI lease",
        )
        require_before(
            init,
            "sharedSpiUnlock();",
            "epd_.setRotation(rotation_);",
            "EPD page setup must occur after initialization SPI ownership is released",
        )
        require_before(
            init,
            "epd_.fillScreen(GxEPD_WHITE);",
            "\"tdeck_pro_epd_init_page\"",
            "EPD page-buffer clearing must not be inside the page SPI lease",
        )
        require(init, "next_page = epd_.nextPage();", "EPD init must fence each nextPage commit")

        require_before(
            render,
            "epd_.drawInvertedBitmap(",
            "\"tdeck_pro_epd_page\"",
            "EPD bitmap rasterization must complete before page SPI ownership is acquired",
        )
        require(render, "next_page = epd_.nextPage();", "EPD frame must submit pages through nextPage")
        require(render, "sharedSpiPrepareDevice(profile().epd.cs);", "EPD page commits must fence peer CS")
        if "epd_.powerOff();" in render:
            raise ValueError(
                "GxEPD2 nextPage() owns the final power-off; do not add a second EPD SPI call"
            )
    except (OSError, ValueError) as error:
        print(f"Shared SPI T-Deck Pro EPD boundary check failed: {error}", file=sys.stderr)
        return 1

    print("Shared SPI T-Deck Pro EPD boundary check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
