#!/usr/bin/env python3
"""Freeze direct shared-SPI lock use to adapter boundaries."""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import os
from pathlib import Path
import re
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh"}
EXCLUDED_DIR_NAMES = {
    ".git",
    ".pio",
    ".pytest_cache",
    ".tmp",
    ".venv",
    "__pycache__",
    "build",
    "dist",
}


@dataclass(frozen=True)
class DirectCallPattern:
    name: str
    pattern: re.Pattern[str]


@dataclass(frozen=True)
class Occurrence:
    path: Path
    relative: str
    line_number: int
    rule: str
    line: str


DIRECT_CALL_PATTERNS = (
    DirectCallPattern("shared_spi_guard", re.compile(r"\bSharedSpiLockGuard\b")),
    DirectCallPattern(
        "shared_spi_lock_with_owner",
        re.compile(r"\bshared_spi_lock_with_owner\s*\("),
    ),
    DirectCallPattern("shared_spi_unlock", re.compile(r"\bshared_spi_unlock\s*\(")),
    DirectCallPattern(
        "lilygo_display_spi_lock",
        re.compile(r"\bLilyGoDispArduinoSPI::lock\s*\("),
    ),
    DirectCallPattern(
        "lilygo_display_spi_unlock",
        re.compile(r"\bLilyGoDispArduinoSPI::unlock\s*\("),
    ),
    DirectCallPattern(
        "lilygo_display_spi_instance_lock",
        re.compile(r"\bspi\.lock\s*\("),
    ),
    DirectCallPattern(
        "lilygo_display_spi_instance_unlock",
        re.compile(r"\bspi\.unlock\s*\("),
    ),
)


# Permanent adapter boundaries are the only places allowed to own physical
# shared-SPI mutex semantics directly.
PERMANENT_ALLOWED_PATHS = {
    "platform/esp/boards/src/display/DisplayInterface.cpp",
    "platform/esp/common/include/platform/esp/common/shared_spi_bus_arbiter.h",
    "platform/esp/common/include/platform/esp/common/shared_spi_lock.h",
    "platform/esp/idf_common/src/shared_spi_lock.cpp",
}


# These files contain adapter-local direct calls, but we pin the exact current
# occurrences so future additions are explicit review points.
PERMANENT_ALLOWED_OCCURRENCES = {
    (
        "platform/esp/arduino_common/src/LV_Helper_v9.cpp",
        "shared_spi_guard",
        "::platform::esp::common::SharedSpiLockGuard spi_guard(",
    ): 9,
    (
        "platform/esp/arduino_common/src/storage/sd_card_runtime.cpp",
        "shared_spi_guard",
        "::platform::esp::common::SharedSpiLockGuard guard_;",
    ): 1,
    (
        "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp",
        "shared_spi_lock_with_owner",
        "::platform::esp::common::shared_spi_lock_with_owner(pdMS_TO_TICKS(timeout_ms),",
    ): 1,
    (
        "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp",
        "shared_spi_lock_with_owner",
        "if (::platform::esp::common::shared_spi_lock_with_owner(kMapTileSdChunkReacquireTicks,",
    ): 1,
    (
        "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp",
        "shared_spi_lock_with_owner",
        "if (::platform::esp::common::shared_spi_lock_with_owner(wait_ticks,",
    ): 1,
    (
        "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp",
        "shared_spi_unlock",
        "::platform::esp::common::shared_spi_unlock();",
    ): 2,
}


# Transitional baseline: these are known active direct calls that still need
# migration to arbiter/token/backpressure paths. The check permits the current
# occurrences to shrink, but any new or changed business-layer direct call fails.
LEGACY_TRANSITION_OCCURRENCES = {
    (
        "boards/tdeck/src/tdeck_board.cpp",
        "lilygo_display_spi_lock",
        "if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(100)))",
    ): 1,
    (
        "boards/tdeck/src/tdeck_board.cpp",
        "lilygo_display_spi_lock",
        "if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))",
    ): 1,
    (
        "boards/tdeck/src/tdeck_board.cpp",
        "lilygo_display_spi_instance_lock",
        "if (!spi.lock(wait_ticks, owner))",
    ): 1,
    (
        "boards/tdeck/src/tdeck_board.cpp",
        "lilygo_display_spi_unlock",
        "LilyGoDispArduinoSPI::unlock();",
    ): 2,
    (
        "boards/tdeck/src/tdeck_board.cpp",
        "lilygo_display_spi_instance_unlock",
        "spi.unlock();",
    ): 1,
    (
        "boards/tlora_pager/src/tlora_pager_board.cpp",
        "lilygo_display_spi_lock",
        "if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(100)))",
    ): 1,
    (
        "boards/tlora_pager/src/tlora_pager_board.cpp",
        "lilygo_display_spi_lock",
        "if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))",
    ): 1,
    (
        "boards/tlora_pager/src/tlora_pager_board.cpp",
        "lilygo_display_spi_instance_lock",
        "if (!spi.lock(wait_ticks, owner))",
    ): 1,
    (
        "boards/tlora_pager/src/tlora_pager_board.cpp",
        "lilygo_display_spi_unlock",
        "LilyGoDispArduinoSPI::unlock();",
    ): 2,
    (
        "boards/tlora_pager/src/tlora_pager_board.cpp",
        "lilygo_display_spi_instance_unlock",
        "spi.unlock();",
    ): 1,
}


def normalize_line(line: str) -> str:
    return re.sub(r"\s+", " ", line.strip())


def occurrence_key(occurrence: Occurrence) -> tuple[str, str, str]:
    return (occurrence.relative, occurrence.rule, normalize_line(occurrence.line))


def git_tracked_files() -> list[Path]:
    try:
        result = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
            cwd=REPO_ROOT,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError):
        return []
    return [REPO_ROOT / line for line in result.stdout.splitlines() if line]


def walk_source_files() -> list[Path]:
    tracked = git_tracked_files()
    if tracked:
        return [path for path in tracked if path.suffix.lower() in SOURCE_SUFFIXES]

    source_files: list[Path] = []
    for root, dir_names, file_names in os.walk(REPO_ROOT):
        dir_names[:] = [name for name in dir_names if name not in EXCLUDED_DIR_NAMES]
        root_path = Path(root)
        for file_name in file_names:
            path = root_path / file_name
            if path.suffix.lower() in SOURCE_SUFFIXES:
                source_files.append(path)
    return source_files


def collect_occurrences() -> list[Occurrence]:
    occurrences: list[Occurrence] = []
    for path in walk_source_files():
        relative = path.relative_to(REPO_ROOT).as_posix()
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue
        for line_number, line in enumerate(lines, start=1):
            for rule in DIRECT_CALL_PATTERNS:
                if rule.pattern.search(line):
                    occurrences.append(
                        Occurrence(
                            path=path,
                            relative=relative,
                            line_number=line_number,
                            rule=rule.name,
                            line=line.rstrip(),
                        )
                    )
    return occurrences


def collect_violations() -> tuple[list[Occurrence], int]:
    permanent_remaining = Counter(PERMANENT_ALLOWED_OCCURRENCES)
    legacy_remaining = Counter(LEGACY_TRANSITION_OCCURRENCES)
    violations: list[Occurrence] = []
    legacy_count = 0

    for occurrence in collect_occurrences():
        if occurrence.relative in PERMANENT_ALLOWED_PATHS:
            continue

        key = occurrence_key(occurrence)
        if permanent_remaining[key] > 0:
            permanent_remaining[key] -= 1
            continue

        if legacy_remaining[key] > 0:
            legacy_remaining[key] -= 1
            legacy_count += 1
            continue

        violations.append(occurrence)

    return violations, legacy_count


def main() -> int:
    violations, legacy_count = collect_violations()
    if not violations:
        print(
            "Shared SPI direct-call boundary check passed "
            f"(legacy transition occurrences remaining: {legacy_count})."
        )
        return 0

    print("Shared SPI direct-call boundary check failed.")
    print(
        "Business/runtime code must acquire shared SPI through an arbiter/token "
        "path; direct physical locks are limited to adapter boundaries."
    )
    for violation in violations:
        print(f"- [{violation.rule}] {violation.relative}:{violation.line_number}")
        print(f"  {violation.line.strip()}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
