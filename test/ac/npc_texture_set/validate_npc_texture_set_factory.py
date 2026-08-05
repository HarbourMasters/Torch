#!/usr/bin/env python3
"""Validate AC:NPC_TEXTURE_SET with a source-free sparse Yaz0 fixture."""

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

SOURCE_OFFSET = 1_447_155_436
LOGICAL_SIZE = 6_137_393
STORED_SIZE = 6_137_408
SET_SIZE = 0x1220
MAX_DECOMPRESSED_SIZE = 24 * 1024 * 1024
SET_OFFSETS = [
    0x474A00, 0x475C20, 0x47C8E0, 0x47DB00, 0x47ED20,
    0x47FF40, 0x481160, 0x482380, 0x4835A0, 0x4847C0,
    0x476E40, 0x478060, 0x479280, 0x47A4A0, 0x47B6C0,
]


def write_sparse(path: Path, chunks: list[tuple[int, bytes]]) -> None:
    with path.open("w+b") as handle:
        if os.name == "nt":
            import msvcrt
            returned = ctypes.c_ulong()
            ok = ctypes.windll.kernel32.DeviceIoControl(
                msvcrt.get_osfhandle(handle.fileno()), 0x000900C4,
                None, 0, None, 0, ctypes.byref(returned), None)
            if not ok:
                raise OSError(ctypes.get_last_error(), "FSCTL_SET_SPARSE failed")
        for offset, data in chunks:
            handle.seek(offset)
            handle.write(data)


def synthetic_yaz0(data: bytes) -> bytes:
    encoded = bytearray(b"Yaz0")
    encoded.extend(len(data).to_bytes(4, "big"))
    encoded.extend(b"\0" * 8)
    position = 0
    while position < len(data):
        code_index = len(encoded)
        encoded.append(0)
        code = 0
        for bit in range(8):
            if position >= len(data):
                break
            run = 0
            if position > 0 and data[position] == data[position - 1]:
                while (run < 273 and position + run < len(data)
                       and data[position + run] == data[position - 1]):
                    run += 1
            if run >= 3:
                if run >= 18:
                    encoded.extend((0, 0, run - 18))
                else:
                    encoded.extend(((run - 2) << 4, 0))
                position += run
            else:
                code |= 0x80 >> bit
                encoded.append(data[position])
                position += 1
        encoded[code_index] = code
    if len(encoded) > LOGICAL_SIZE:
        raise RuntimeError("synthetic Yaz0 fixture exceeds the exact source member")
    encoded.extend(b"\0" * (LOGICAL_SIZE - len(encoded)))
    encoded.extend(bytes(range(1, STORED_SIZE - LOGICAL_SIZE + 1)))
    return bytes(encoded)


def recipe(variant: int, edits: dict[str, object] | None = None) -> str:
    fields: dict[str, object] = {
        "offset": 0,
        "species": "cat",
        "variant": variant,
        "source_member": "/foresta.rel.szs",
        "compressed_logical_size": LOGICAL_SIZE,
        "compressed_stored_size": STORED_SIZE,
        "texture_set_offset": SET_OFFSETS[variant - 1] if 1 <= variant <= 15 else 0,
        "texture_set_size": SET_SIZE,
        "palette_size": 0x20,
        "eye_count": 8,
        "mouth_count": 6,
        "frame_size": 0x100,
        "body_size": 0x400,
        "frame_width": 32,
        "frame_height": 16,
        "format": "C4",
        "palette_format": "RGB5A3",
        "destination_path": f"__OTR__ac/texture/npc/cat/cat-{variant:02d}.ANTX",
    }
    range_fields: dict[str, object] = {
        "source_offset": SOURCE_OFFSET,
        "size": STORED_SIZE,
        "packed_offset": 0,
    }
    for key, value in (edits or {}).items():
        if key.startswith("range_"):
            range_fields[key.removeprefix("range_")] = value
        else:
            fields[key] = value
    lines = [
        f"cat_{variant:02d}:",
        "  type: AC:NPC_TEXTURE_SET",
        "  path: source.bin",
        "  bounded_ranges:",
        f"    - source_offset: {range_fields['source_offset']}",
        f"      size: {range_fields['size']}",
        f"      packed_offset: {range_fields['packed_offset']}",
    ]
    lines.extend(f"  {key}: {value}" for key, value in fields.items())
    return "\n".join(lines)


def configure(root: Path, body: str) -> None:
    (root / "assets").mkdir(parents=True)
    (root / "assets" / "root.yml").write_text(body + "\n", encoding="utf-8")
    (root / "config.yml").write_text(
        "mode: directory\nfolder: npc-texture-set\npath: assets\nconfig:\n"
        "  sort: OFFSET\n  logging: CRITICAL\n  output:\n"
        "    binary: game.o2r\n",
        encoding="utf-8",
    )


def run(torch: Path, root: Path, destination: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(torch), "o2r", "source.bin", "-s", ".", "-d", destination],
        cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=os.environ.copy(), check=False, timeout=180,
    )


def validate_entry(payload: bytes, expected: bytes) -> None:
    if len(payload) != 96 + SET_SIZE:
        raise RuntimeError(f"unexpected ANTX entry size: {len(payload)}")
    if payload[:4] != b"\x01\0\0\0" or payload[4:8] != b"ANTX":
        raise RuntimeError("ANTX resource header is invalid")
    if payload[64:68] != b"ACNT":
        raise RuntimeError("ANTX payload magic is invalid")
    expected_metadata = (
        (16).to_bytes(2, "big") +
        (8).to_bytes(2, "big") +
        (6).to_bytes(2, "big") +
        (32).to_bytes(2, "big") +
        (16).to_bytes(2, "big") +
        (8).to_bytes(2, "big") +
        (2).to_bytes(2, "big") +
        b"\0\0" +
        (0x100).to_bytes(4, "big") +
        (0x400).to_bytes(4, "big") +
        SET_SIZE.to_bytes(4, "big")
    )
    if payload[68:96] != expected_metadata:
        raise RuntimeError("ANTX layout metadata is invalid")
    if payload[96:] != expected:
        raise RuntimeError("ANTX source bytes changed")


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

    rel_size = max(offset + SET_SIZE for offset in SET_OFFSETS)
    rel = bytearray(rel_size)
    for variant, offset in enumerate(SET_OFFSETS, 1):
        rel[offset:offset + SET_SIZE] = bytes(
            ((variant * 37 + index * 11) & 0xFF) for index in range(SET_SIZE)
        )
    compressed = synthetic_yaz0(bytes(rel))

    try:
        positive = work / "positive"
        configure(positive, "\n".join(recipe(variant) for variant in range(1, 16)))
        write_sparse(positive / "source.bin", [(SOURCE_OFFSET, compressed)])
        result = run(args.torch.resolve(), positive, "out")
        if result.returncode:
            raise RuntimeError(
                "positive extraction failed\n" +
                result.stdout + result.stderr
            )
        archive_path = positive / "out" / "game.o2r"
        family = hashlib.sha256()
        with zipfile.ZipFile(archive_path) as archive:
            expected_names = [
                f"ac/texture/npc/cat/cat-{variant:02d}.ANTX"
                for variant in range(1, 16)
            ] + ["version"]
            if archive.namelist() != expected_names:
                raise RuntimeError(f"unexpected archive inventory/order: {archive.namelist()}")
            for variant, offset in enumerate(SET_OFFSETS, 1):
                payload = archive.read(f"ac/texture/npc/cat/cat-{variant:02d}.ANTX")
                validate_entry(payload, bytes(rel[offset:offset + SET_SIZE]))
                family.update(payload)

        negatives = {
            "species": {"species": "dog"},
            "variant": {"variant": 0},
            "set-offset": {"texture_set_offset": SET_OFFSETS[0] + 1},
            "destination": {
                "destination_path": "__OTR__ac/texture/npc/cat/cat-02.ANTX"
            },
            "range-offset": {"range_source_offset": SOURCE_OFFSET + 1},
            "range-size": {"range_size": STORED_SIZE - 1},
            "range-packed": {"range_packed_offset": 1},
            "logical-size": {"compressed_logical_size": LOGICAL_SIZE - 1},
            "stored-size": {"compressed_stored_size": STORED_SIZE - 1},
            "eye-count": {"eye_count": 7},
            "format": {"format": "C8"},
            "generic-offset": {"offset": 1},
            "source-base": {"source_base_offset": 0},
        }
        for name, edits in negatives.items():
            case = work / f"negative-{name}"
            configure(case, recipe(1, edits))
            os.link(positive / "source.bin", case / "source.bin")
            if run(args.torch.resolve(), case, "out").returncode == 0:
                raise RuntimeError(f"negative case unexpectedly passed: {name}")

        corrupt = work / "negative-yaz0"
        configure(corrupt, recipe(1))
        write_sparse(corrupt / "source.bin", [(SOURCE_OFFSET, b"Bad!" + compressed[4:])])
        if run(args.torch.resolve(), corrupt, "out").returncode == 0:
            raise RuntimeError("corrupt Yaz0 case unexpectedly passed")

        oversized = work / "negative-oversized-output"
        configure(oversized, recipe(1))
        oversized_source = bytearray(compressed)
        oversized_source[4:8] = (MAX_DECOMPRESSED_SIZE + 1).to_bytes(4, "big")
        write_sparse(oversized / "source.bin", [(SOURCE_OFFSET, bytes(oversized_source))])
        if run(args.torch.resolve(), oversized, "out").returncode == 0:
            raise RuntimeError("oversized Yaz0 output unexpectedly passed")

        undersized = work / "negative-undersized-output"
        configure(undersized, recipe(1))
        undersized_source = bytearray(compressed)
        undersized_source[4:8] = (max(SET_OFFSETS) + SET_SIZE - 1).to_bytes(4, "big")
        write_sparse(undersized / "source.bin", [(SOURCE_OFFSET, bytes(undersized_source))])
        if run(args.torch.resolve(), undersized, "out").returncode == 0:
            raise RuntimeError("undersized Yaz0 output unexpectedly passed")

        print(
            "AC:NPC_TEXTURE_SET validation passed: "
            f"entries=15 family_sha256={family.hexdigest()} "
            f"negatives={len(negatives) + 3}"
        )
        return 0
    except Exception as exc:
        print(f"AC:NPC_TEXTURE_SET validation failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if not args.keep_work:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
