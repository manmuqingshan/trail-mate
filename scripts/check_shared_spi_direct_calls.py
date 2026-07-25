#!/usr/bin/env python3
"""Enforce the single shared-SPI coordinator boundary.

The old check allowed a growing list of direct-lock exceptions. That made the
exception list part of the architecture and allowed the split-brain ownership
model to return. The new check intentionally has one rule: the legacy SPI
lock/arbiter names must not exist in production source. Shared-SPI callers use
SharedSpiCoordinator through the generic bus token API.
"""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh"}
EXCLUDED_PARTS = {
    ".git",
    ".pio",
    "build",
    "builds",
    ".distinction",
    "docs",
    ".codex-build",
    ".codex-build-logs",
}

FORBIDDEN_NAMES = (
    "shared_spi_lock.h",
    "shared_spi_lock.cpp",
    "shared_spi_lock_with_owner",
    "shared_spi_unlock",
    "SharedSpiLockGuard",
    "SharedSpiBusAdapter",
    "FixedSharedSpiBusPolicyStrategy",
)

REQUIRED_FILES = (
    REPO_ROOT / "platform/esp/common/include/platform/esp/common/shared_spi_coordinator.h",
    REPO_ROOT / "platform/esp/common/src/shared_spi_coordinator.cpp",
)


def source_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=REPO_ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    paths: list[Path] = []
    for name in result.stdout.splitlines():
        path = REPO_ROOT / name
        if (
            path.suffix.lower() in SOURCE_SUFFIXES
            and not any(part in EXCLUDED_PARTS for part in path.relative_to(REPO_ROOT).parts)
            and path.name != Path(__file__).name
        ):
            paths.append(path)
    return paths


def main() -> int:
    missing = [str(path.relative_to(REPO_ROOT)) for path in REQUIRED_FILES if not path.exists()]
    violations: list[str] = []

    for path in source_files():
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for line_number, line in enumerate(text.splitlines(), start=1):
            for forbidden in FORBIDDEN_NAMES:
                if forbidden in line:
                    violations.append(
                        f"{path.relative_to(REPO_ROOT).as_posix()}:{line_number}: {forbidden}"
                    )

    if missing or violations:
        print("Shared SPI coordinator contract check failed.")
        for item in missing:
            print(f"- missing required file: {item}")
        for item in violations:
            print(f"- forbidden legacy symbol: {item}")
        return 1

    print("Shared SPI coordinator contract check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
