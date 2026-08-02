#!/usr/bin/env python3
"""Validate the complete AC item-billboard family without game data."""

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
SPECS = [
    ("apple", 0x66AB40, 0x66AB20, 32),
    ("axe", 0xB6AAA0, 0xB6AA80, 32),
    ("axe2", 0xB6AD60, 0xB6AD40, 32),
    ("bag", 0xB6DC20, 0xB6DC00, 32),
    ("bone", 0xB6DEE0, 0xB6DEC0, 32),
    ("box", 0xB6E1A0, 0xB6E180, 32),
    ("cage", 0xB6E460, 0xB6E440, 32),
    ("carpet", 0xB6B020, 0xB6B000, 32),
    ("cloth", 0xB6EA00, 0xB6E9E0, 32),
    ("coco", 0xB6ECC0, 0xB6ECA0, 32),
    ("diary", 0xB6EF80, 0xB6EF60, 32),
    ("fish", 0x66BD18, 0x66BCF8, 32),
    ("fork", 0xB6F560, 0xB6F540, 32),
    ("fossil", 0xB6F820, 0xB6F800, 32),
    ("fuku", 0xB6B2E0, 0xB6B2C0, 32),
    ("haniwa", 0xB6FAE0, 0xB6FAC0, 32),
    ("kabu", 0x66C9C0, 0x66C9A0, 32),
    ("kaza", 0xB6B5A0, 0xB6B580, 32),
    ("leaf", 0xB70060, 0xB70040, 32),
    ("matutake", 0x672A80, 0x672A60, 32),
    ("net", 0xB6B860, 0xB6B840, 32),
    ("net2", 0xB6BB20, 0xB6BB00, 32),
    ("nuts", 0xB708A0, 0xB70880, 32),
    ("omikuji", 0xB70B60, 0xB70B40, 32),
    ("orange", 0x673C00, 0x673BE0, 32),
    ("other", 0xB710E0, 0xB710C0, 32),
    ("otosi", 0xB713A0, 0xB71380, 32),
    ("pack", 0xB71660, 0xB71640, 32),
    ("paper", 0xB6BDE0, 0xB6BDC0, 32),
    ("peach", 0xB71920, 0xB71900, 32),
    ("pear", 0xB71BE0, 0xB71BC0, 32),
    ("present", 0xB71EA0, 0xB71E80, 32),
    ("rod", 0xB6C0A0, 0xB6C080, 32),
    ("rod2", 0xB6C360, 0xB6C340, 32),
    ("roll", 0xB72160, 0xB72140, 32),
    ("seed", 0xB6C620, 0xB6C600, 32),
    ("shell-a", 0xB72560, 0xB72540, 16),
    ("shell-b", 0xB726A0, 0xB72680, 16),
    ("shell-c", 0xB727E0, 0xB727C0, 16),
    ("shovel", 0xB6C8E0, 0xB6C8C0, 32),
    ("shovel2", 0xB6CBA0, 0xB6CB80, 32),
    ("taisou", 0xB6CE60, 0xB6CE40, 32),
    ("tane", 0x3ECA60, 0x3ECA40, 16),
    ("ticket", 0xB6D120, 0xB6D100, 32),
    ("tool", 0xB72920, 0xB72900, 32),
    ("trash", 0xB72BE0, 0xB72BC0, 32),
    ("umbrella", 0xB72EA0, 0xB72E80, 32),
    ("utiwa", 0xB6D3E0, 0xB6D3C0, 32),
    ("wall", 0xB6D6A0, 0xB6D680, 32),
]
REQUIRED_OUTPUT_SIZE = max(
    max(texture + width * width // 2, palette + 32)
    for _, texture, palette, width in SPECS
)
EXPECTED_FAMILY_BYTES = 192_336


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


def recipe(spec: tuple[str, int, int, int], edits: dict[str, object] | None = None) -> str:
    name, texture, palette, width = spec
    fields: dict[str, object] = {
        "offset": 0,
        "item_name": name,
        "source_member": "/foresta.rel.szs",
        "compressed_logical_size": LOGICAL_SIZE,
        "compressed_stored_size": STORED_SIZE,
        "texture_offset": texture,
        "palette_offset": palette,
        "texture_size": width * width // 2,
        "palette_size": 32,
        "width": width,
        "height": width,
        "format": "C4",
        "palette_format": "RGB5A3",
        "palette_entries": 16,
        "destination_path": f"__OTR__ac/texture/item/{name}.OTEX",
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
        f"{name}:",
        "  type: AC:ITEM_BILLBOARD_TEXTURE",
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
        "mode: directory\nfolder: item-billboard\npath: assets\nconfig:\n"
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


def decode_color(value: int) -> bytes:
    if value & 0x8000:
        return bytes((
            (((value >> 10) & 31) << 3) | ((value >> 12) & 7),
            (((value >> 5) & 31) << 3) | ((value >> 7) & 7),
            ((value & 31) << 3) | ((value >> 2) & 7),
            255,
        ))
    alpha = (value >> 12) & 7
    return bytes((
        ((value >> 8) & 15) * 17,
        ((value >> 4) & 15) * 17,
        (value & 15) * 17,
        (alpha * 255 + 3) // 7,
    ))


def decode_c4(image: bytes, palette: bytes, width: int) -> bytes:
    colors = [
        decode_color(int.from_bytes(palette[index:index + 2], "big"))
        for index in range(0, 32, 2)
    ]
    rgba = bytearray(width * width * 4)
    tiles = width // 8
    for tile_y in range(tiles):
        for tile_x in range(tiles):
            tile_base = (tile_y * tiles + tile_x) * 32
            for y in range(8):
                for x in range(8):
                    packed = image[tile_base + y * 4 + x // 2]
                    index = packed >> 4 if x % 2 == 0 else packed & 15
                    destination = ((tile_y * 8 + y) * width + tile_x * 8 + x) * 4
                    rgba[destination:destination + 4] = colors[index]
    return bytes(rgba)


def validate_entry(payload: bytes, image: bytes, palette: bytes, width: int) -> None:
    expected = decode_c4(image, palette, width)
    if len(payload) != 80 + len(expected):
        raise RuntimeError(f"unexpected OTEX size: {len(payload)}")
    if payload[:4] != b"\x01\0\0\0" or payload[4:8] != b"OTEX":
        raise RuntimeError("OTEX resource header is invalid")
    metadata = (
        b"ACTX" + width.to_bytes(2, "big") + width.to_bytes(2, "big")
        + (1).to_bytes(4, "big") + len(expected).to_bytes(4, "big")
    )
    if payload[64:80] != metadata or payload[80:] != expected:
        raise RuntimeError("OTEX decoded texture differs")


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

    rel = bytearray(REQUIRED_OUTPUT_SIZE)
    fixtures: dict[str, tuple[bytes, bytes, int]] = {}
    for item_index, (name, texture, palette, width) in enumerate(SPECS):
        palette_values = []
        for index in range(16):
            if index % 2 == 0:
                value = (0x8000 | (((index * 2 + item_index) & 31) << 10)
                         | (((index * 3) & 31) << 5) | ((index * 5) & 31))
            else:
                value = (((index & 7) << 12) | (((index + item_index) & 15) << 8)
                         | (((index * 3) & 15) << 4) | ((index * 5) & 15))
            palette_values.append(value)
        palette_bytes = b"".join(value.to_bytes(2, "big") for value in palette_values)
        image_bytes = bytes(
            (((index + item_index) & 15) << 4) | ((index * 3 + item_index) & 15)
            for index in range(width * width // 2)
        )
        rel[palette:palette + 32] = palette_bytes
        rel[texture:texture + len(image_bytes)] = image_bytes
        fixtures[name] = (image_bytes, palette_bytes, width)
    compressed = synthetic_yaz0(bytes(rel))

    try:
        positive = work / "positive"
        configure(positive, "\n".join(recipe(spec) for spec in SPECS))
        write_sparse(positive / "source.bin", [(SOURCE_OFFSET, compressed)])
        result = run(args.torch.resolve(), positive, "out")
        if result.returncode:
            raise RuntimeError(
                "positive extraction failed\n" +
                result.stdout + result.stderr
            )
        archive_path = positive / "out" / "game.o2r"

        family = hashlib.sha256()
        family_bytes = 0
        with zipfile.ZipFile(archive_path) as archive:
            expected_names = [f"ac/texture/item/{name}.OTEX" for name, *_ in SPECS] + ["version"]
            if archive.namelist() != expected_names:
                raise RuntimeError(f"unexpected archive inventory/order: {archive.namelist()}")
            for name, *_ in SPECS:
                image, palette, width = fixtures[name]
                payload = archive.read(f"ac/texture/item/{name}.OTEX")
                validate_entry(payload, image, palette, width)
                family.update(payload)
                family_bytes += len(payload)
        if family_bytes != EXPECTED_FAMILY_BYTES:
            raise RuntimeError(f"unexpected serialized family size: {family_bytes}")

        ordinary = next(spec for spec in SPECS if spec[0] == "bag")
        highest = next(spec for spec in SPECS if spec[0] == "umbrella")
        negatives = {
            "unknown-member": {"item_name": "unknown"},
            "member-tuple": {"texture_offset": ordinary[1] + 1},
            "destination": {"destination_path": "__OTR__ac/texture/item/present.OTEX"},
            "layout": {"width": 16},
            "source-member": {"source_member": "/static.str"},
            "compressed-size": {"compressed_logical_size": LOGICAL_SIZE - 1},
            "source-range": {"range_source_offset": SOURCE_OFFSET + 1},
            "generic-offset": {"offset": 1},
            "source-base": {"source_base_offset": 0},
        }
        for case_name, edits in negatives.items():
            case = work / f"negative-{case_name}"
            configure(case, recipe(ordinary, edits))
            os.link(positive / "source.bin", case / "source.bin")
            if run(args.torch.resolve(), case, "out").returncode == 0:
                raise RuntimeError(f"negative case unexpectedly passed: {case_name}")

        undersized = work / "negative-undersized-output"
        configure(undersized, recipe(highest))
        undersized_source = bytearray(compressed)
        undersized_source[4:8] = (REQUIRED_OUTPUT_SIZE - 1).to_bytes(4, "big")
        write_sparse(undersized / "source.bin", [(SOURCE_OFFSET, bytes(undersized_source))])
        if run(args.torch.resolve(), undersized, "out").returncode == 0:
            raise RuntimeError("undersized Yaz0 output unexpectedly passed")

        print(
            "AC:ITEM_BILLBOARD_TEXTURE validation passed: "
            f"entries={len(SPECS)} family_sha256={family.hexdigest()} "
            f"bytes={family_bytes} negatives={len(negatives) + 1}"
        )
        return 0
    except Exception as exc:
        print(f"AC:ITEM_BILLBOARD_TEXTURE validation failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if not args.keep_work:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
