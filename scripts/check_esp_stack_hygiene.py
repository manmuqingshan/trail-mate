#!/usr/bin/env python3
"""Reject high-risk ESP stack allocations in protocol hot paths."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os
import re
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh"}
IMPLEMENTATION_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}

HOT_PATH_PREFIXES = (
    "platform/esp/arduino_common/src/chat/infra/meshtastic/",
    "modules/core_phone/src/meshtastic/",
)

HOT_PATH_FILES = {
    "platform/esp/arduino_common/src/ble/meshtastic_ble.cpp",
}

HOT_HEADER_PREFIXES = (
    "platform/esp/arduino_common/include/platform/esp/arduino_common/chat/infra/meshtastic/",
    "modules/core_phone/include/phone/meshtastic/",
)

HOT_HEADER_FILES = {
    "platform/esp/arduino_common/include/ble/meshtastic_ble.h",
}

APP_CONFIG_PATHS = {
    "platform/esp/arduino_common/src/app_config_store.cpp",
    "platform/esp/arduino_common/src/app_context.cpp",
}

HIGH_RISK_PROTOCOL_TYPES = (
    "MeshtasticBleFrame",
    "phone::meshtastic::MeshtasticBleFrame",
    "meshtastic_FromRadio",
    "meshtastic_ToRadio",
    "meshtastic_MeshPacket",
    "meshtastic_Data",
    "meshtastic_AdminMessage",
    "meshtastic_MqttClientProxyMessage",
    "meshtastic_ServiceEnvelope",
)

HIGH_RISK_CONFIG_TYPES = (
    "AppConfig",
    "app::AppConfig",
    "MeshConfig",
    "chat::MeshConfig",
)

RAW_ARRAY_STACK_LIMIT = 192

LARGE_ARRAY_SIZE_TOKENS = (
    "MeshtasticBleFrame::kMaxPayload",
    "meshtastic_FromRadio_size",
    "meshtastic_ToRadio_size",
    "kMaxEncoded",
    "MAX_PACKET_SIZE",
    "TM_C6_MAX_PAYLOAD",
)


@dataclass(frozen=True)
class Violation:
    path: Path
    line_number: int
    rule: str
    line: str
    detail: str


def normalize(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def is_under(relative: str, prefixes: tuple[str, ...]) -> bool:
    return any(relative.startswith(prefix) for prefix in prefixes)


def git_tracked_files() -> list[Path]:
    try:
        result = subprocess.run(
            ["git", "ls-files"],
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
    excluded_dirs = {".git", ".pio", ".tmp", "build", "dist", "third_party"}
    for root, dir_names, file_names in os.walk(REPO_ROOT):
        dir_names[:] = [name for name in dir_names if name not in excluded_dirs]
        root_path = Path(root)
        for file_name in file_names:
            path = root_path / file_name
            if path.suffix.lower() in SOURCE_SUFFIXES:
                source_files.append(path)
    return source_files


def type_decl_pattern(type_name: str) -> re.Pattern[str]:
    escaped = re.escape(type_name)
    return re.compile(
        rf"^\s*(?!static\b)(?:(?:const|volatile)\s+)*{escaped}\s+"
        rf"([A-Za-z_][A-Za-z0-9_]*)\s*(?:[={{;(])"
    )


PROTOCOL_TYPE_PATTERNS = tuple(
    (type_name, type_decl_pattern(type_name)) for type_name in HIGH_RISK_PROTOCOL_TYPES
)
CONFIG_TYPE_PATTERNS = tuple(
    (type_name, type_decl_pattern(type_name)) for type_name in HIGH_RISK_CONFIG_TYPES
)

RAW_ARRAY_PATTERN = re.compile(
    r"^\s*(?!static\b)(?:(?:const|volatile)\s+)*"
    r"(?:uint8_t|std::uint8_t|char|unsigned\s+char|std::byte)\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*([A-Za-z0-9_:]+)\s*\]"
)

STD_ARRAY_PATTERN = re.compile(
    r"^\s*(?!static\b)(?:(?:const|volatile)\s+)*"
    r"std::array\s*<\s*(?:uint8_t|std::uint8_t|char|unsigned\s+char|std::byte)\s*,\s*"
    r"([A-Za-z0-9_:]+)\s*>\s+([A-Za-z_][A-Za-z0-9_]*)"
)

STD_DEQUE_PATTERN = re.compile(r"\bstd::deque\s*<")


def is_large_array_size(size_token: str) -> bool:
    if size_token.isdigit():
        return int(size_token) >= RAW_ARRAY_STACK_LIMIT
    return size_token in LARGE_ARRAY_SIZE_TOKENS


def collect_violations() -> list[Violation]:
    violations: list[Violation] = []

    for path in walk_source_files():
        relative = normalize(path)
        suffix = path.suffix.lower()
        in_hot_impl = suffix in IMPLEMENTATION_SUFFIXES and (
            relative in HOT_PATH_FILES or is_under(relative, HOT_PATH_PREFIXES)
        )
        in_hot_header = relative in HOT_HEADER_FILES or is_under(relative, HOT_HEADER_PREFIXES)
        in_app_config = relative in APP_CONFIG_PATHS

        if not in_hot_impl and not in_hot_header and not in_app_config:
            continue

        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError as exc:
            violations.append(Violation(path, 0, "read-error", "", str(exc)))
            continue

        for line_number, line in enumerate(lines, start=1):
            stripped = line.strip()
            if not stripped or stripped.startswith("//"):
                continue

            if in_hot_header and STD_DEQUE_PATTERN.search(line):
                violations.append(
                    Violation(
                        path,
                        line_number,
                        "esp-hot-path-std-deque",
                        stripped,
                        "Use a fixed-depth ring buffer with an explicit drop policy.",
                    )
                )

            if in_hot_impl:
                for type_name, pattern in PROTOCOL_TYPE_PATTERNS:
                    if pattern.search(line):
                        violations.append(
                            Violation(
                                path,
                                line_number,
                                "esp-hot-path-large-local",
                                stripped,
                                f"Move local {type_name} storage to member scratch/static storage or fill an existing slot.",
                            )
                        )

                raw_array = RAW_ARRAY_PATTERN.search(line)
                if raw_array and is_large_array_size(raw_array.group(2)):
                    violations.append(
                        Violation(
                            path,
                            line_number,
                            "esp-hot-path-large-array",
                            stripped,
                            "Move large raw byte arrays off the task stack.",
                        )
                    )

                std_array = STD_ARRAY_PATTERN.search(line)
                if std_array and is_large_array_size(std_array.group(1)):
                    violations.append(
                        Violation(
                            path,
                            line_number,
                            "esp-hot-path-large-std-array",
                            stripped,
                            "Move large std::array byte buffers off the task stack.",
                        )
                    )

            if in_app_config:
                for type_name, pattern in CONFIG_TYPE_PATTERNS:
                    if pattern.search(line):
                        violations.append(
                            Violation(
                                path,
                                line_number,
                                "esp-app-config-large-local",
                                stripped,
                                f"Do not place {type_name} on app/config task stacks; use existing context storage or scratch.",
                            )
                        )

    return violations


def main() -> int:
    violations = collect_violations()
    if not violations:
        print("ESP stack hygiene check passed.")
        return 0

    print("ESP stack hygiene check failed:")
    for violation in violations:
        relative = normalize(violation.path)
        location = f"{relative}:{violation.line_number}" if violation.line_number else relative
        print(f"- [{violation.rule}] {location}")
        if violation.line:
            print(f"  {violation.line}")
        print(f"  {violation.detail}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
