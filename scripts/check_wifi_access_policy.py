#!/usr/bin/env python3
"""Guard Wi-Fi resource access boundaries from regressing."""

from __future__ import annotations

import os
from pathlib import Path
import re
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

HTTP_ALLOWED = {
    "platform/esp/arduino_common/src/platform_ui_http_client_runtime.cpp",
}

WIFI_CONNECT_FORBIDDEN_ROOTS = (
    "platform/esp/arduino_common/src/chat",
    "platform/esp/arduino_common/src/ui/runtime",
)

WIFI_CONNECT_FORBIDDEN_FILES = {
    "platform/esp/arduino_common/src/platform_ui_firmware_update_runtime.cpp",
    "platform/esp/arduino_common/src/platform_ui_route_storage.cpp",
}

WIFI_CONNECT_ALLOWED = {
    "platform/esp/arduino_common/src/platform_ui_wifi_access_runtime.cpp",
}

HTTP_PATTERNS = (
    re.compile(r"#include\s*[<\"]esp_http_client\.h[>\"]"),
    re.compile(r"\besp_http_client_[a-zA-Z0-9_]+\s*\("),
)

WIFI_CONNECT_PATTERNS = (
    re.compile(r"\bplatform::ui::wifi::connect\s*\("),
    re.compile(r"\bwifi_runtime::connect\s*\("),
)


def iter_source_files(root: Path):
    for current_root, dir_names, file_names in os.walk(root):
        dir_names[:] = [
            name
            for name in dir_names
            if name not in EXCLUDED_DIR_NAMES and not name.startswith(".codex-")
        ]
        current_root_path = Path(current_root)
        for file_name in file_names:
            path = current_root_path / file_name
            if path.suffix.lower() in SOURCE_SUFFIXES:
                yield path


def rel(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def is_wifi_business_file(relative: str) -> bool:
    if relative in WIFI_CONNECT_FORBIDDEN_FILES:
        return True
    return any(relative.startswith(root + "/") for root in WIFI_CONNECT_FORBIDDEN_ROOTS)


def main() -> int:
    violations: list[tuple[str, int, str, str]] = []
    for path in iter_source_files(REPO_ROOT):
        relative = rel(path)
        text = path.read_text(encoding="utf-8", errors="ignore")
        for line_number, line in enumerate(text.splitlines(), start=1):
            if relative not in HTTP_ALLOWED and any(pattern.search(line) for pattern in HTTP_PATTERNS):
                violations.append((relative, line_number, "direct-esp-http-client", line.strip()))
            if (
                relative not in WIFI_CONNECT_ALLOWED
                and is_wifi_business_file(relative)
                and any(pattern.search(line) for pattern in WIFI_CONNECT_PATTERNS)
            ):
                violations.append((relative, line_number, "direct-wifi-connect", line.strip()))

    if not violations:
        print("Wi-Fi access policy check passed.")
        return 0

    print("Wi-Fi access policy check failed:")
    for relative, line_number, rule, line in violations:
        print(f"- [{rule}] {relative}:{line_number}")
        print(f"  {line}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
