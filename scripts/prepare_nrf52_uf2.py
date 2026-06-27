from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
NRF52840_UF2_FAMILY_ID = 0xADA52840


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create and verify a UF2 firmware image for a built nRF52840 PlatformIO env."
    )
    parser.add_argument("--env", required=True, help="PlatformIO environment name")
    parser.add_argument(
        "--build-root",
        default=".pio/build",
        help="Root directory that contains PlatformIO build outputs",
    )
    parser.add_argument(
        "--family",
        default=f"0x{NRF52840_UF2_FAMILY_ID:08X}",
        help="UF2 family passed to the Adafruit nRF52 uf2conv tool",
    )
    parser.add_argument(
        "--expected-family",
        default=f"0x{NRF52840_UF2_FAMILY_ID:08X}",
        help="Required UF2 family ID after conversion",
    )
    parser.add_argument(
        "--uf2conv",
        help="Optional explicit path to framework-arduinoadafruitnrf52/tools/uf2conv/uf2conv.py",
    )
    return parser.parse_args()


def parse_int(value: str) -> int:
    return int(value, 0)


def candidate_uf2conv_paths(explicit_path: str | None) -> list[Path]:
    candidates: list[Path] = []
    if explicit_path:
        candidates.append(Path(explicit_path))

    platformio_core_dir = os.environ.get("PLATFORMIO_CORE_DIR")
    if platformio_core_dir:
        candidates.append(
            Path(platformio_core_dir)
            / "packages"
            / "framework-arduinoadafruitnrf52"
            / "tools"
            / "uf2conv"
            / "uf2conv.py"
        )

    candidates.append(
        Path.home()
        / ".platformio"
        / "packages"
        / "framework-arduinoadafruitnrf52"
        / "tools"
        / "uf2conv"
        / "uf2conv.py"
    )
    return candidates


def find_uf2conv(explicit_path: str | None) -> Path:
    candidates = candidate_uf2conv_paths(explicit_path)
    for path in candidates:
        if path.exists():
            return path

    raise FileNotFoundError(
        "Could not find Adafruit nRF52 uf2conv.py. Checked:\n  "
        + "\n  ".join(str(path) for path in candidates)
    )


def read_u32_le(block: bytes, offset: int) -> int:
    return int.from_bytes(block[offset : offset + 4], "little")


def verify_uf2_family(path: Path, expected_family: int) -> None:
    data = path.read_bytes()
    if len(data) == 0 or len(data) % 512 != 0:
        raise RuntimeError(f"Invalid UF2 size for {path}: {len(data)} bytes")

    block_count = len(data) // 512
    seen_families: set[int] = set()

    for index in range(block_count):
        block = data[index * 512 : (index + 1) * 512]
        magic_start0 = read_u32_le(block, 0)
        magic_start1 = read_u32_le(block, 4)
        flags = read_u32_le(block, 8)
        payload_size = read_u32_le(block, 16)
        block_no = read_u32_le(block, 20)
        num_blocks = read_u32_le(block, 24)
        family_id = read_u32_le(block, 28)
        magic_end = read_u32_le(block, 508)

        if magic_start0 != UF2_MAGIC_START0 or magic_start1 != UF2_MAGIC_START1:
            raise RuntimeError(f"Invalid UF2 magic at block {index}")
        if magic_end != UF2_MAGIC_END:
            raise RuntimeError(f"Invalid UF2 end magic at block {index}")
        if payload_size > 476:
            raise RuntimeError(f"Invalid UF2 payload size at block {index}: {payload_size}")
        if num_blocks != block_count:
            raise RuntimeError(
                f"Invalid UF2 block count at block {index}: header={num_blocks} actual={block_count}"
            )
        if block_no >= block_count:
            raise RuntimeError(f"Invalid UF2 block number at block {index}: {block_no}")
        if flags & UF2_FLAG_FAMILY_ID_PRESENT == 0:
            raise RuntimeError(f"UF2 block {index} is missing the family-id-present flag")

        seen_families.add(family_id)

    if seen_families != {expected_family}:
        formatted = ", ".join(f"0x{family:08X}" for family in sorted(seen_families))
        raise RuntimeError(
            f"Unexpected UF2 family ID(s): {formatted}; expected 0x{expected_family:08X}"
        )


def main() -> int:
    args = parse_args()
    build_dir = Path(args.build_root) / args.env
    firmware_hex = build_dir / "firmware.hex"
    firmware_uf2 = build_dir / "firmware.uf2"
    uf2conv = find_uf2conv(args.uf2conv)
    expected_family = parse_int(args.expected_family)

    if not firmware_hex.exists():
        raise FileNotFoundError(f"Missing nRF52 firmware hex: {firmware_hex}")

    command = [
        sys.executable,
        str(uf2conv),
        "-c",
        "-f",
        args.family,
        "-o",
        str(firmware_uf2),
        str(firmware_hex),
    ]

    env = dict(os.environ)
    env.setdefault("PYTHONWARNINGS", "ignore::SyntaxWarning")

    print(f"Creating nRF52 UF2 for {args.env} -> {firmware_uf2}", flush=True)
    subprocess.run(command, check=True, env=env)

    if not firmware_uf2.exists() or firmware_uf2.stat().st_size == 0:
        raise RuntimeError(f"UF2 output was not created: {firmware_uf2}")

    verify_uf2_family(firmware_uf2, expected_family)
    print(f"Verified UF2 family ID: 0x{expected_family:08X}", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
