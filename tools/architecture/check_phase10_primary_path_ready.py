#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def require_file(rel: str, failures: list[str]) -> None:
    if not (ROOT / rel).is_file():
        failures.append(f"missing required file: {rel}")


def require_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        failures.append(f"missing file for token check: {rel}")
        return
    text = read(rel)
    for token in tokens:
        if token not in text:
            failures.append(f"{rel} missing token: {token}")


def forbid_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        return
    text = read(rel)
    for token in tokens:
        if token in text:
            failures.append(f"{rel} contains forbidden token: {token}")


def check_primary_file(
    rel: str,
    required: list[str],
    forbidden: list[str],
    failures: list[str],
) -> None:
    require_tokens(rel, required, failures)
    forbid_tokens(rel, forbidden, failures)


def main() -> int:
    failures: list[str] = []

    required_files = [
        "apps/linux_sim_shell/src/linux_sim_runtime_entry.h",
        "apps/linux_sim_shell/src/linux_sim_runtime_entry.cpp",
        "apps/linux_sim_shell/tests/linux_sim_runtime_entry_smoke.cpp",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_adoption.h",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_adoption.cpp",
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_page_registry_adoption_smoke.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_primary_screen_graph_runtime.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_primary_screen_graph_runtime.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_primary_screen_graph_runtime.cpp",
        "docs/audits/PHASE10_PRIMARY_PATH_MIGRATION_REPORT.md",
        "docs/audits/PHASE10_FINAL_PRIMARY_PATH_REPORT.md",
    ]
    for rel in required_files:
        require_file(rel, failures)

    check_primary_file(
        "apps/linux_sim_shell/src/linux_sim_runtime_entry.h",
        [
            "LinuxSimRuntimeSource",
            "Unavailable",
            "ScreenGraphAdoption",
            "usingPrimaryScreenGraph",
            "runtimeSource",
        ],
        [
            "HardcodedFallback",
            "fallbackUsed",
            "startFallback",
            "findUxPackById",
            "UxPackRegistry",
            "buildMenuForUxPack",
            "ui::menu::MenuModel",
            "GtkWidget",
            "lv_obj_t",
        ],
        failures,
    )
    check_primary_file(
        "apps/linux_sim_shell/src/linux_sim_runtime_entry.cpp",
        [
            "LinuxSimRuntimeSource::ScreenGraphAdoption",
            "LinuxSimRuntimeSource::Unavailable",
            "usingPrimaryScreenGraph",
        ],
        [
            "HardcodedFallback",
            "fallbackUsed",
            "startFallback",
            "findUxPackById",
            "UxPackRegistry",
            "buildMenuForUxPack",
            "ui::menu::MenuModel",
            "GtkWidget",
            "lv_obj_t",
        ],
        failures,
    )
    require_tokens(
        "apps/linux_sim_shell/tests/linux_sim_runtime_entry_smoke.cpp",
        [
            "usingPrimaryScreenGraph",
            "runtimeSource",
            "LinuxSimRuntimeSource::",
            "ScreenGraphAdoption",
        ],
        failures,
    )

    check_primary_file(
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_adoption.h",
        [
            "LinuxUConsoleGtkPageRegistrySource",
            "Unavailable",
            "ScreenGraphAdoption",
            "usingPrimaryScreenGraph",
            "registrySource",
        ],
        [
            "HardcodedFallback",
            "fallbackUsed",
            "loadFallback",
            "findUxPackById",
            "UxPackRegistry",
            "buildMenuForUxPack",
            "GtkWidget",
            "lv_obj_t",
        ],
        failures,
    )
    check_primary_file(
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_adoption.cpp",
        [
            "LinuxUConsoleGtkPageRegistrySource::ScreenGraphAdoption",
            "LinuxUConsoleGtkPageRegistrySource::Unavailable",
            "usingPrimaryScreenGraph",
        ],
        [
            "HardcodedFallback",
            "fallbackUsed",
            "loadFallback",
            "findUxPackById",
            "UxPackRegistry",
            "buildMenuForUxPack",
            "GtkWidget",
            "lv_obj_t",
        ],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_page_registry_adoption_smoke.cpp",
        [
            "usingPrimaryScreenGraph",
            "registrySource",
            "LinuxUConsoleGtkPageRegistrySource::ScreenGraphAdoption",
        ],
        failures,
    )

    check_primary_file(
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_primary_screen_graph_runtime.h",
        [
            "LvglPrimaryScreenGraphRuntime",
            "LvglScreenGraphRuntimeSource",
            "Unavailable",
            "ScreenGraphAdoption",
            "usingPrimaryScreenGraph",
            "runtimeSource",
        ],
        [
            "HardcodedFallback",
            "fallbackUsed",
            "loadFallback",
            "lvgl.h",
            "lv_obj_t",
            "BOARD_",
            "findUxPackById",
            "UxPackRegistry",
            "buildMenuForUxPack",
        ],
        failures,
    )
    check_primary_file(
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_primary_screen_graph_runtime.cpp",
        [
            "LvglScreenGraphRuntimeSource::ScreenGraphAdoption",
            "LvglScreenGraphRuntimeSource::Unavailable",
            "usingPrimaryScreenGraph",
        ],
        [
            "HardcodedFallback",
            "fallbackUsed",
            "loadFallback",
            "lvgl.h",
            "lv_obj_t",
            "BOARD_",
            "findUxPackById",
            "UxPackRegistry",
            "buildMenuForUxPack",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_primary_screen_graph_runtime.cpp",
        [
            "LvglPrimaryScreenGraphRuntime",
            "LvglScreenGraphRuntimeSource::ScreenGraphAdoption",
            "LvglScreenGraphRuntimeSource::Unavailable",
            "usingPrimaryScreenGraph",
            "route.valid",
        ],
        failures,
    )
    require_tokens(
        "cmake/TrailMateUxPacks.cmake",
        ["lvgl_primary_screen_graph_runtime.cpp"],
        failures,
    )
    require_tokens(
        "apps/linux_sim_shell/CMakeLists.txt",
        [
            "linux_sim_runtime_entry_smoke.cpp",
            "linux_sim_runtime_renderer_smoke.cpp",
            "lvgl_primary_screen_graph_runtime",
        ],
        failures,
    )
    forbid_tokens(
        "apps/linux_sim_shell/CMakeLists.txt",
        ["linux_sim_runtime_entry_fallback_smoke.cpp"],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/CMakeLists.txt",
        [
            "linux_uconsole_gtk_page_registry_adoption_smoke.cpp",
            "linux_uconsole_gtk_page_registry_renderer_smoke.cpp",
        ],
        failures,
    )
    forbid_tokens(
        "apps/linux_uconsole_gtk/CMakeLists.txt",
        ["linux_uconsole_gtk_page_registry_fallback_smoke.cpp"],
        failures,
    )

    require_tokens(
        "docs/audits/PHASE10_PRIMARY_PATH_MIGRATION_REPORT.md",
        [
            "LinuxSim Primary Path",
            "GTK Primary Path",
            "LVGL Primary Descriptor Path",
            "unavailable-on-failure",
            "LVGL fallback deleted",
            "Phase 11 Recommendation",
            "AsciiRuntimeEntryAdoption",
            "GtkRuntimeEntryAdoption",
            "LvglPrimaryScreenGraphRuntime",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE10_FINAL_PRIMARY_PATH_REPORT.md",
        [
            "Primary Path Status",
            "Unavailable on failed adoption",
            "LVGL failed adoption is unavailable-on-failure",
            "Not Done",
            "Phase 11 Recommendation",
            "Real Renderer Descriptor Consumption",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md",
        [
            "deleted after LinuxSim/uConsole fallback burn-down",
            "LVGL hardcoded menu/page creation: exit condition satisfied",
            "LvglPrimaryScreenGraphRuntime",
        ],
        failures,
    )

    if failures:
        for failure in failures:
            print(f"[phase10-primary-path] {failure}")
        return 1

    print("[phase10-primary-path] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
