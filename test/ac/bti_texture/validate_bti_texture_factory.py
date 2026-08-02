#!/usr/bin/env python3
"""Exercise the exact AC:BTI_TEXTURE boy1 factory with synthetic input."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ENTRY = "ac/texture/forest_2nd/data/boy1.OTEX"
MEMBER_OFFSET = 1454147680
MEMBER_SIZE = 2432
WIDTH = 32
HEIGHT = 64
PALETTE_ENTRIES = 176
IMAGE_OFFSET = 0x20
IMAGE_SIZE = 2048
PALETTE_OFFSET = 0x820
PALETTE_SIZE = 352
HEADER = bytes.fromhex(
    "0902002000400000010200b000000820"
    "00000000010100000100000000000020"
)
EXPECTED_HEADER_SHA256 = "765cdc45862318e912b481276efb237a30c7f2afecf5b1b6301857d1db4ee249"
EXPECTED_RGBA_SHA256 = "bcd02c20ab6b4fa6ab82de357e59ecc475207948e32a97155d2ae73d86249111"
RESOURCE_HEADER = (
    b"\x01\x00\x00\x00OTEX\x00\x00\x00\x00"
    b"\xde\xad\xbe\xef\xde\xad\xbe\xef" + bytes(44)
)


def synthetic_bti() -> bytes:
    image = bytes(index % PALETTE_ENTRIES for index in range(IMAGE_SIZE))
    palette = bytearray()
    for index in range(PALETTE_ENTRIES):
        if index % 3 == 0:
            value = (
                (((index // 3) & 7) << 12) |
                (((index * 3) & 15) << 8) |
                (((index * 5) & 15) << 4) |
                ((index * 7) & 15))
        else:
            value = (
                0x8000 | (((index * 11) & 31) << 10) |
                (((index * 13) & 31) << 5) | ((index * 17) & 31))
        palette += value.to_bytes(2, "big")
    member = HEADER + image + bytes(palette)
    if len(member) != MEMBER_SIZE:
        raise RuntimeError(f"synthetic BTI size mismatch: {len(member)}")
    return member


def write_sparse(path: Path, member: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w+b") as handle:
        if os.name == "nt":
            import msvcrt
            returned = ctypes.c_ulong()
            ok = ctypes.windll.kernel32.DeviceIoControl(
                msvcrt.get_osfhandle(handle.fileno()), 0x000900C4,
                None, 0, None, 0, ctypes.byref(returned), None)
            if not ok:
                raise OSError(ctypes.get_last_error(), "FSCTL_SET_SPARSE failed")
        handle.seek(MEMBER_OFFSET)
        handle.write(member)


def config(root: Path, member: bytes, *, offset: int = 0,
           declared_size: int = MEMBER_SIZE,
           destination: str = f"__OTR__{ENTRY}",
           ranges: list[dict[str, int]] | None = None,
           source_base: bool = False) -> None:
    (root / "assets").mkdir(parents=True)
    write_sparse(root / "source.bin", member)
    selected = ([{
        "source_offset": MEMBER_OFFSET,
        "size": MEMBER_SIZE,
        "packed_offset": 0,
    }] if ranges is None else ranges)
    lines = [
        "boy1_texture:",
        "  type: AC:BTI_TEXTURE",
        "  path: source.bin",
        "  bounded_ranges:",
    ]
    for item in selected:
        lines.append(f"    - source_offset: {item['source_offset']}")
        lines.append(f"      size: {item['size']}")
        lines.append(f"      packed_offset: {item['packed_offset']}")
    if source_base:
        lines.append("  source_base_offset: 0")
    lines += [
        f"  offset: {offset}",
        f"  size: {declared_size}",
        f"  destination_path: {destination}",
    ]
    (root / "assets" / "root.yml").write_text(
        "\n".join(lines) + "\n", encoding="utf-8")
    (root / "config.yml").write_text(
        "mode: directory\nfolder: ac-bti-texture\npath: assets\nconfig:\n"
        "  sort: OFFSET\n  logging: CRITICAL\n  output:\n    binary: boy1.o2r\n",
        encoding="utf-8",
    )


def run(torch: Path, root: Path, destination: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(torch), "o2r", "source.bin", "-s", ".", "-d", destination],
        cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=os.environ.copy(), check=False, timeout=180,
    )


def archive_rgba(archive: Path) -> bytes:
    with zipfile.ZipFile(archive) as handle:
        if handle.namelist() != [ENTRY, "version"]:
            raise RuntimeError(f"archive entries differed: {handle.namelist()}")
        data = handle.read(ENTRY)
    if (len(data) != 8272 or data[:64] != RESOURCE_HEADER or data[64:68] != b"ACTX"
            or data[68:72] != b"\x00 \x00@" or data[72:76] != (1).to_bytes(4, "big")
            or data[76:80] != (8192).to_bytes(4, "big")):
        raise RuntimeError("boy1 OTEX/ACTX shape mismatch")
    return data[80:]


def expected_rgba(member: bytes) -> bytes:
    image = member[IMAGE_OFFSET:IMAGE_OFFSET + IMAGE_SIZE]
    palette = member[PALETTE_OFFSET:PALETTE_OFFSET + PALETTE_SIZE]
    colors: list[tuple[int, int, int, int]] = []
    for index in range(PALETTE_ENTRIES):
        value = int.from_bytes(palette[index * 2:index * 2 + 2], "big")
        if value & 0x8000:
            red = (value >> 10) & 31
            green = (value >> 5) & 31
            blue = value & 31
            colors.append((
                (red << 3) | (red >> 2),
                (green << 3) | (green >> 2),
                (blue << 3) | (blue >> 2),
                255,
            ))
        else:
            alpha = (value >> 12) & 7
            colors.append((
                ((value >> 8) & 15) * 17,
                ((value >> 4) & 15) * 17,
                (value & 15) * 17,
                (alpha * 255 + 3) // 7,
            ))

    result = bytearray(WIDTH * HEIGHT * 4)
    tiles_across = WIDTH // 8
    tiles_down = HEIGHT // 4
    for tile_y in range(tiles_down):
        for tile_x in range(tiles_across):
            tile_base = (tile_y * tiles_across + tile_x) * 32
            for y in range(4):
                for x in range(8):
                    color = colors[image[tile_base + y * 8 + x]]
                    destination = ((tile_y * 4 + y) * WIDTH + tile_x * 8 + x) * 4
                    result[destination:destination + 4] = bytes(color)
    return bytes(result)


def expect_member_reject(torch: Path, work: Path, name: str, member: bytes) -> None:
    case = work / f"negative-member-{name}"
    config(case, member)
    if run(torch, case, "out").returncode == 0:
        raise RuntimeError(f"member negative unexpectedly passed: {name}")


def expect_schema_reject(torch: Path, work: Path, name: str, **configuration) -> None:
    case = work / f"negative-schema-{name}"
    config(case, synthetic_bti(), **configuration)
    if run(torch, case, "out").returncode == 0:
        raise RuntimeError(f"schema negative unexpectedly passed: {name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--torch", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--keep-work", action="store_true")
    args = parser.parse_args()
    work = args.work_dir.resolve()
    if work.exists():
        parser.error("--work-dir must not already exist")
    work.mkdir(parents=True)
    try:
        if hashlib.sha256(HEADER).hexdigest() != EXPECTED_HEADER_SHA256:
            raise RuntimeError("fixed boy1 header hash mismatch")
        member = synthetic_bti()
        expected = expected_rgba(member)
        if hashlib.sha256(expected).hexdigest() != EXPECTED_RGBA_SHA256:
            raise RuntimeError(
                f"fixed synthetic RGBA hash mismatch: {hashlib.sha256(expected).hexdigest()}")

        positive = work / "positive"
        config(positive, member)
        result = run(args.torch.resolve(), positive, "out")
        if result.returncode:
            raise RuntimeError(
                "synthetic boy1 positive failed\n" +
                result.stdout + result.stderr)
        actual = archive_rgba(positive / "out" / "boy1.o2r")
        if actual != expected:
            raise RuntimeError("synthetic boy1 differs from the independent decoder")
        if all(alpha == 255 for alpha in actual[3::4]):
            raise RuntimeError("RGB5A3 translucent-palette coverage is missing")

        mutation = bytearray(member)
        mutation[0] ^= 1
        expect_member_reject(args.torch.resolve(), work, "header", bytes(mutation))
        palette_index = bytearray(member)
        palette_index[IMAGE_OFFSET] = PALETTE_ENTRIES
        expect_member_reject(
            args.torch.resolve(), work, "palette-index", bytes(palette_index))
        expect_member_reject(args.torch.resolve(), work, "truncated", member[:-1])

        exact_range = {
            "source_offset": MEMBER_OFFSET,
            "size": MEMBER_SIZE,
            "packed_offset": 0,
        }
        schema_negatives = {
            "generic-offset": {"offset": 1},
            "declared-size-short": {"declared_size": MEMBER_SIZE - 1},
            "declared-size-extra": {"declared_size": MEMBER_SIZE + 1},
            "destination": {"destination": "__OTR__ac/texture/not-boy1.OTEX"},
            "source-base": {"source_base": True},
            "range-count": {"ranges": [exact_range, {
                "source_offset": MEMBER_OFFSET,
                "size": 1,
                "packed_offset": MEMBER_SIZE,
            }]},
            "source-offset": {"ranges": [{
                **exact_range, "source_offset": MEMBER_OFFSET + 1}]},
            "range-size-short": {"ranges": [{
                **exact_range, "size": MEMBER_SIZE - 1}]},
            "range-size-extra": {"ranges": [{
                **exact_range, "size": MEMBER_SIZE + 1}]},
            "packed-offset": {"ranges": [{
                **exact_range, "packed_offset": 1}]},
            "overflow": {"ranges": [{
                "source_offset": 18446744073709551615,
                "size": 2,
                "packed_offset": 0,
            }]},
        }
        for name, configuration in schema_negatives.items():
            expect_schema_reject(
                args.torch.resolve(), work, name, **configuration)

        negatives = 3 + len(schema_negatives)
        print("AC:BTI_TEXTURE bounded validation passed: "
              f"rgba_sha256={EXPECTED_RGBA_SHA256} negatives={negatives}")
        return 0
    except Exception as exc:
        print(f"AC:BTI_TEXTURE validation failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if not args.keep_work:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
