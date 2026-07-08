#!/usr/bin/env python3
"""Guard Reticulum runtime budget boundaries from regressing."""

from __future__ import annotations

from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent
LXMF_CPP = REPO_ROOT / "platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_adapter.cpp"
LXMF_H = REPO_ROOT / "platform/esp/arduino_common/include/platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"
RT_CPP = REPO_ROOT / "platform/esp/arduino_common/src/chat/infra/reticulum/reticulum_adapter.cpp"
RT_H = REPO_ROOT / "platform/esp/arduino_common/include/platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"
RTDIR_CPP = REPO_ROOT / "platform/esp/arduino_common/src/platform_ui_reticulum_directory_runtime.cpp"

LARGE_LOCAL_PATTERN = re.compile(
    r"\buint8_t\s+\w+\s*\[\s*"
    r"(?:kMaxPacketLen|kMaxLxmfMessageLen|kSignedPartMaxLen|kMaxTokenPlaintextLen)"
    r"\s*\]"
)


def method_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise ValueError(f"missing method signature: {signature}")
    brace = text.find("{", start)
    if brace < 0:
        raise ValueError(f"missing method body: {signature}")
    depth = 0
    for index in range(brace, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1 : index]
    raise ValueError(f"unterminated method body: {signature}")


def main() -> int:
    violations: list[str] = []
    lxmf_cpp = LXMF_CPP.read_text(encoding="utf-8")
    lxmf_h = LXMF_H.read_text(encoding="utf-8")
    rt_cpp = RT_CPP.read_text(encoding="utf-8")
    rt_h = RT_H.read_text(encoding="utf-8")
    rtdir_cpp = RTDIR_CPP.read_text(encoding="utf-8")

    for signature in (
        "bool LxmfAdapter::pollIncomingText",
        "bool LxmfAdapter::pollIncomingData",
    ):
        body = method_body(lxmf_cpp, signature)
        for forbidden in ("processRadioPackets", "processRuntime", "maybeAnnounce"):
            if forbidden in body:
                violations.append(f"{signature} must not call {forbidden}()")

    if "maybePersistPeers(true)" in lxmf_cpp:
        violations.append("Reticulum RX paths must not force maybePersistPeers(true)")

    maybe_persist_body = method_body(lxmf_cpp, "bool LxmfAdapter::maybePersistPeers")
    non_force_branch = maybe_persist_body.split("const bool ok = persistPeers()", 1)[0]
    if "if (!force)" not in non_force_branch or "return true;" not in non_force_branch:
        violations.append("maybePersistPeers(false) must return before persistPeers()")

    for stale_name in ("kWifiDiscoverySampleIntervalMs", "consumeWifiDiscoveryBudget"):
        if stale_name in lxmf_cpp or stale_name in lxmf_h:
            violations.append(f"stale Wi-Fi-only discovery budget name remains: {stale_name}")

    if "void processSendQueue() override;" not in rt_h:
        violations.append("ReticulumAdapter must expose processSendQueue() as the runtime pump hook")

    if not re.search(r"void\s+ReticulumAdapter::processSendQueue\s*\(\s*\)\s*\{[^{}]*service_->processSendQueue\s*\(\s*\)", rt_cpp, re.S):
        violations.append("ReticulumAdapter::processSendQueue() must forward to the LXMF service")

    process_pos = lxmf_cpp.find("bool LxmfAdapter::processOneRadioPacket")
    defer_pos = lxmf_cpp.find("shouldDeferDiscoveryPacket", process_pos)
    remember_pos = lxmf_cpp.find("rememberPacket(packet_hash)", process_pos)
    if process_pos < 0 or defer_pos < 0 or remember_pos < 0 or defer_pos > remember_pos:
        violations.append("Reticulum packets must be eligible for discovery deferral before rememberPacket()")

    if "record_announce(directory_announce)" in method_body(lxmf_cpp, "bool LxmfAdapter::handleAnnouncePacket"):
        announce_body = method_body(lxmf_cpp, "bool LxmfAdapter::handleAnnouncePacket")
        record_pos = announce_body.find("record_announce(directory_announce)")
        gate_pos = announce_body.rfind("if (allow_persistence)", 0, record_pos)
        if gate_pos < 0:
            violations.append("record_announce() must be gated by allow_persistence")

    record_announce_body = method_body(rtdir_cpp, "Status record_announce(")
    record_address_body = method_body(rtdir_cpp, "Status record_lxmf_address(")
    if "queue_announce_async(record)" not in record_announce_body:
        violations.append("record_announce() must queue, not synchronously upsert SD")
    if "queue_lxmf_address_async(record)" not in record_address_body:
        violations.append("record_lxmf_address() must queue, not synchronously upsert SD")
    if "read_byte()" in rtdir_cpp:
        violations.append("Reticulum directory TSV reads must use chunked LineReader, not read_byte()")

    for signature in (
        "bool LxmfAdapter::sendAnnounce",
        "bool LxmfAdapter::handleAnnouncePacket",
    ):
        body = method_body(lxmf_cpp, signature)
        if LARGE_LOCAL_PATTERN.search(body):
            violations.append(f"{signature} must not allocate Reticulum packet buffers on the mesh_task stack")

    if violations:
        print("Reticulum runtime budget policy check failed:")
        for violation in violations:
            print(f"- {violation}")
        return 1

    print("Reticulum runtime budget policy check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
