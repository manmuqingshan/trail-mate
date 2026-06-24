#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

ACTIVE_ROOTS = [
    "apps",
    "builds",
    "modules",
    "platform",
    "boards",
    "cmake",
]
GENERATED_BUILD_PREFIXES = (
    "builds/linux_cmake/build/",
    "builds/pio_nrf52/.pio/",
)


def iter_files(base: Path):
    if not base.exists():
        return
    for path in base.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith(GENERATED_BUILD_PREFIXES):
            continue
        yield path


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def main() -> int:
    failures: list[str] = []
    retired_historical_descriptor_tokens = [
        "historical_source_descriptor",
        "HistoricalSourceDescriptor",
        "historical_root_name",
        "historical_generic_root_name",
        "historical_board_root_name",
        "replacement_owner",
    ]

    if (ROOT / "legacy").exists():
        failures.append("root legacy/ directory must not exist")

    for root_name in ACTIVE_ROOTS:
        for path in iter_files(ROOT / root_name):
            text = read(path)
            rel = path.relative_to(ROOT).as_posix()
            if "legacy/app_implementations" in text:
                failures.append(
                    f"{rel} contains forbidden legacy/app_implementations reference"
                )
            if root_name == "apps" and "legacy_source_descriptor" in rel:
                failures.append(
                    f"{rel} contains forbidden legacy_source_descriptor filename"
                )
            if root_name in {"apps", "builds", "cmake"}:
                if "historical_source_descriptor" in rel:
                    failures.append(
                        f"{rel} contains forbidden historical_source_descriptor filename"
                    )
                for token in retired_historical_descriptor_tokens:
                    if token in text:
                        failures.append(
                            f"{rel} contains retired historical descriptor token: {token}"
                        )

    if not (ROOT / "docs/archive/REMOVED_LEGACY_ROOTS.md").is_file():
        failures.append("missing docs/archive/REMOVED_LEGACY_ROOTS.md")

    archive_root = ROOT / "docs/archive"
    if archive_root.exists():
        for path in archive_root.rglob("*"):
            if path.is_dir() and path.name in {"legacy", "app_implementations"}:
                failures.append(
                    f"docs/archive must not retain source archive directory: {path.relative_to(ROOT).as_posix()}"
                )

    if failures:
        for failure in failures:
            print(f"[no-root-legacy] {failure}")
        return 1

    print("[no-root-legacy] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
