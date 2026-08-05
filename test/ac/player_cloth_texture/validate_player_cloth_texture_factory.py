#!/usr/bin/env python3
"""Exercise the bounded AC:PLAYER_CLOTH_TEXTURE factory with synthetic inputs."""

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

IMAGE_SOURCE_BASE = 1454014656
PALETTE_SOURCE_BASE = 1453900320
PLAYER_CLOTH_COUNT = 255


def specification(cloth_index: int) -> dict[str, int | str]:
    return {
        "entry": f"ac/texture/forest_1st/player/cloth-{cloth_index:03d}.OTEX",
        "image_source_offset": IMAGE_SOURCE_BASE + cloth_index * 512,
        "palette_source_offset": PALETTE_SOURCE_BASE + cloth_index * 32,
    }


SPECIFICATIONS = {
    index: specification(index) for index in range(PLAYER_CLOTH_COUNT)
}
EXPECTED_RGBA_SHA256 = "2ceef1598e28c0329d75887aa65d60a9dea92245f5bc9160ecbb29429fd5ed69"
EXPECTED_FAMILY_SHA256 = "f6f43c31fd0c34357be08983e1c6d33a30670c97edaf71310b9e27a1a37d74ea"
EXPECTED_PIXELS = {
    (0, 0): (0, 0, 0, 255),
    (7, 0): (24, 49, 74, 255),
    (8, 0): (8, 16, 24, 255),
    (0, 8): (33, 66, 99, 255),
    (15, 17): (16, 33, 49, 255),
    (31, 31): (99, 198, 33, 255),
}


def synthetic_ranges(cloth_index: int) -> tuple[bytes, bytes]:
    image = bytearray()
    for tile in range(16):
        for pixel_pair in range(32):
            high = (cloth_index + tile + pixel_pair) & 15
            low = (cloth_index * 5 + tile * 3 + pixel_pair) & 15
            image.append((high << 4) | low)
    palette = bytearray()
    for index in range(16):
        if cloth_index == 0:
            value = 0x8000 | (index << 10) | ((index * 2 & 31) << 5) | (index * 3 & 31)
        elif index % 3 == 0:
            value = (
                (((cloth_index + index) & 7) << 12) |
                (((cloth_index * 3 + index) & 15) << 8) |
                (((cloth_index * 5 + index * 2) & 15) << 4) |
                ((cloth_index * 7 + index * 3) & 15))
        else:
            value = 0x8000 | ((cloth_index * 109 + index * 1057) & 0x7FFF)
        palette += value.to_bytes(2, "big")
    return bytes(image), bytes(palette)


def write_sparse(path: Path, chunks: list[tuple[int, bytes]]) -> None:
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
        for offset, data in chunks:
            handle.seek(offset)
            handle.write(data)


def default_ranges(cloth_index: int) -> list[dict[str, int]]:
    specification = SPECIFICATIONS[cloth_index]
    return [
        {"source_offset": specification["image_source_offset"],
         "size": 512, "packed_offset": 0},
        {"source_offset": specification["palette_source_offset"],
         "size": 32, "packed_offset": 512},
    ]


def configure(root: Path, image: bytes, palette: bytes, *,
              cloth_index: int = 0,
              edits: dict[str, object] | None = None,
              ranges: list[dict[str, int]] | None = None,
              source_base: bool = False) -> None:
    specification = SPECIFICATIONS.get(cloth_index, SPECIFICATIONS[0])
    selected_ranges = (
        default_ranges(cloth_index)
        if ranges is None and cloth_index in SPECIFICATIONS
        else default_ranges(0) if ranges is None else ranges
    )
    (root / "assets").mkdir(parents=True)
    write_sparse(root / "source.bin", [
        (specification["palette_source_offset"], palette),
        (specification["image_source_offset"], image),
    ])
    fields: dict[str, object] = {
        "offset": 0, "image_offset": 0, "image_size": 512,
        "palette_offset": 512, "palette_size": 32, "cloth_index": cloth_index,
        "width": 32, "height": 32, "format": "C4",
        "palette_format": "RGB5A3", "palette_entries": 16,
        "destination_path": f"__OTR__{specification['entry']}",
    }
    if edits:
        fields.update(edits)
    lines = ["cloth:", "  type: AC:PLAYER_CLOTH_TEXTURE", "  path: source.bin",
             "  bounded_ranges:"]
    for item in selected_ranges:
        lines.append(f"    - source_offset: {item['source_offset']}")
        lines.append(f"      size: {item['size']}")
        lines.append(f"      packed_offset: {item['packed_offset']}")
    if source_base:
        lines.append("  source_base_offset: 0")
    lines += [f"  {key}: {value}" for key, value in fields.items()]
    (root / "assets" / "root.yml").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (root / "config.yml").write_text(
        "mode: directory\nfolder: cloth\npath: assets\nconfig:\n"
        "  sort: OFFSET\n  logging: CRITICAL\n  output:\n    binary: cloth.o2r\n",
        encoding="utf-8",
    )


def configure_family(root: Path) -> None:
    (root / "assets").mkdir(parents=True)
    generated = {
        index: synthetic_ranges(index) for index in range(PLAYER_CLOTH_COUNT)
    }
    chunks = [
        (int(SPECIFICATIONS[index]["palette_source_offset"]), generated[index][1])
        for index in range(PLAYER_CLOTH_COUNT)
    ] + [
        (int(SPECIFICATIONS[index]["image_source_offset"]), generated[index][0])
        for index in range(PLAYER_CLOTH_COUNT)
    ]
    write_sparse(root / "source.bin", chunks)

    lines: list[str] = []
    for index in range(PLAYER_CLOTH_COUNT):
        item = SPECIFICATIONS[index]
        lines += [
            f"cloth_{index:03d}:",
            "  type: AC:PLAYER_CLOTH_TEXTURE",
            "  path: source.bin",
            "  bounded_ranges:",
            f"    - source_offset: {item['image_source_offset']}",
            "      size: 512",
            "      packed_offset: 0",
            f"    - source_offset: {item['palette_source_offset']}",
            "      size: 32",
            "      packed_offset: 512",
            "  offset: 0",
            "  image_offset: 0",
            "  image_size: 512",
            "  palette_offset: 512",
            "  palette_size: 32",
            f"  cloth_index: {index}",
            "  width: 32",
            "  height: 32",
            "  format: C4",
            "  palette_format: RGB5A3",
            "  palette_entries: 16",
            f"  destination_path: __OTR__{item['entry']}",
        ]
    (root / "assets" / "root.yml").write_text(
        "\n".join(lines) + "\n", encoding="utf-8")
    (root / "config.yml").write_text(
        "mode: directory\nfolder: cloth\npath: assets\nconfig:\n"
        "  sort: OFFSET\n  logging: CRITICAL\n  output:\n    binary: cloth.o2r\n",
        encoding="utf-8",
    )


def run(torch: Path, root: Path, destination: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(torch), "o2r", "source.bin", "-s", ".", "-d", destination],
        cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=os.environ.copy(), check=False, timeout=180,
    )


def rgba(data: bytes) -> bytes:
    if (len(data) != 4176 or data[:1] != b"\x01" or data[4:8] != b"OTEX"
            or data[8:12] != bytes(4) or data[64:68] != b"ACTX"
            or data[68:72] != b"\x00 \x00 " or data[72:76] != (1).to_bytes(4, "big")
            or data[76:80] != (4096).to_bytes(4, "big")):
        raise RuntimeError("cloth OTEX/ACTX shape mismatch")
    return data[80:]


def family_rgba(archive: Path) -> dict[int, bytes]:
    with zipfile.ZipFile(archive) as handle:
        expected_names = [
            str(SPECIFICATIONS[index]["entry"])
            for index in range(PLAYER_CLOTH_COUNT)
        ] + ["version"]
        if handle.namelist() != expected_names:
            raise RuntimeError("complete cloth archive names or order differ")
        return {
            index: rgba(handle.read(str(SPECIFICATIONS[index]["entry"])))
            for index in range(PLAYER_CLOTH_COUNT)
        }


def expected_rgba(image: bytes, palette: bytes) -> bytes:
    colors = []
    for index in range(16):
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
    result = bytearray(4096)
    for tile_y in range(4):
        for tile_x in range(4):
            tile_base = (tile_y * 4 + tile_x) * 32
            for y in range(8):
                for x in range(8):
                    packed = image[tile_base + y * 4 + x // 2]
                    color = colors[packed >> 4 if x % 2 == 0 else packed & 15]
                    destination = ((tile_y * 8 + y) * 32 + tile_x * 8 + x) * 4
                    result[destination:destination + 4] = bytes(color)
    return bytes(result)


def assert_oracle(data: bytes) -> None:
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_RGBA_SHA256:
        raise RuntimeError(f"fixed synthetic RGBA hash mismatch: {digest}")
    for (x, y), expected in EXPECTED_PIXELS.items():
        offset = (y * 32 + x) * 4
        actual = tuple(data[offset:offset + 4])
        if actual != expected:
            raise RuntimeError(f"fixed pixel oracle mismatch at {(x, y)}: {actual}")


def reject(torch: Path, work: Path, name: str, image: bytes, palette: bytes,
           **configuration) -> None:
    case = work / f"negative-{name}"
    configure(case, image, palette, **configuration)
    if run(torch, case, "out").returncode == 0:
        raise RuntimeError(f"negative case unexpectedly passed: {name}")


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
        image, palette = synthetic_ranges(0)
        positive = work / "positive-family"
        configure_family(positive)
        result = run(args.torch.resolve(), positive, "out")
        if result.returncode:
            raise RuntimeError(
                "synthetic 255-cloth family positive failed\n" +
                result.stdout + result.stderr)
        outputs = family_rgba(positive / "out" / "cloth.o2r")
        for cloth_index, actual in outputs.items():
            expected = expected_rgba(*synthetic_ranges(cloth_index))
            if actual != expected:
                raise RuntimeError(
                    f"cloth-{cloth_index:03d} differs from the independent decoder")
        assert_oracle(outputs[0])
        if all(alpha == 255 for alpha in outputs[1][3::4]):
            raise RuntimeError("RGB5A3 translucent-palette coverage is missing")
        family_digest = hashlib.sha256(b"".join(
            outputs[index] for index in range(PLAYER_CLOTH_COUNT))).hexdigest()
        if family_digest != EXPECTED_FAMILY_SHA256:
            raise RuntimeError(f"fixed 255-cloth family hash mismatch: {family_digest}")

        reject(args.torch.resolve(), work, "truncated-image", image[:-1], palette)
        field_negatives = {
            "generic-offset": {"offset": 1},
            "extra-image-range": {"image_size": 513},
            "extra-palette-range": {"palette_size": 33},
            "image-packed-offset": {"image_offset": 1},
            "palette-packed-offset": {"palette_offset": 513},
            "cloth-index-low": {"cloth_index": -1},
            "cloth-index-high": {"cloth_index": PLAYER_CLOTH_COUNT},
            "width": {"width": 31},
            "height": {"height": 31},
            "format": {"format": "C8"},
            "palette-format": {"palette_format": "RGB565"},
            "palette-entries": {"palette_entries": 15},
            "destination": {"destination_path": "__OTR__ac/texture/not-cloth.OTEX"},
        }
        for name, edits in field_negatives.items():
            reject(args.torch.resolve(), work, name, image, palette, edits=edits)
        reject(
            args.torch.resolve(), work, "cloth-index-far", image, palette,
            cloth_index=PLAYER_CLOTH_COUNT + 1)
        cloth_index = 127
        reject(
            args.torch.resolve(), work, "wrong-destination", image, palette,
            cloth_index=cloth_index,
            edits={"destination_path": f"__OTR__{SPECIFICATIONS[0]['entry']}"})
        reject(
            args.torch.resolve(), work, "wrong-image-source", image, palette,
            cloth_index=cloth_index,
            ranges=[default_ranges(0)[0], default_ranges(cloth_index)[1]])
        reject(
            args.torch.resolve(), work, "wrong-palette-source", image, palette,
            cloth_index=cloth_index,
            ranges=[default_ranges(cloth_index)[0], default_ranges(0)[1]])
        reject(args.torch.resolve(), work, "source-base", image, palette, source_base=True)

        exact = default_ranges(0)
        range_negatives = {
            "range-count-short": exact[:1],
            "range-count-extra": exact + [
                {"source_offset": SPECIFICATIONS[0]["image_source_offset"],
                 "size": 1, "packed_offset": 544}],
            "range-order": [exact[1], exact[0]],
            "range-source-offset": [
                {**exact[0], "source_offset":
                    SPECIFICATIONS[0]["image_source_offset"] + 1}, exact[1]],
            "range-size-short": [{**exact[0], "size": 511}, exact[1]],
            "range-size-extra": [{**exact[0], "size": 513}, exact[1]],
            "range-packed-gap": [{**exact[0], "packed_offset": 1}, exact[1]],
            "range-overflow": [
                {"source_offset": 18446744073709551615, "size": 2, "packed_offset": 0},
                exact[1]],
        }
        for name, ranges in range_negatives.items():
            reject(args.torch.resolve(), work, name, image, palette, ranges=ranges)

        total_negatives = (
            1 + len(field_negatives) + 1 + 3 +
            1 + len(range_negatives))
        print("AC:PLAYER_CLOTH_TEXTURE bounded validation passed: "
              f"entries={PLAYER_CLOTH_COUNT} rgba_sha256={EXPECTED_RGBA_SHA256} "
              f"family_sha256={EXPECTED_FAMILY_SHA256} "
              f"negatives={total_negatives}")
        return 0
    except Exception as exc:
        print(f"AC:PLAYER_CLOTH_TEXTURE validation failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if not args.keep_work:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
