#!/usr/bin/env python3
"""Validate deterministic, root-relative O2R packing without game data."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import zipfile


BASE_ENTRIES = ["alpha.txt", "nested/beta.bin", "nested/deeper/gamma.txt"]


def run_torch(torch: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(torch), *arguments],
        capture_output=True,
        check=False,
        text=True,
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def pack(torch: Path, source: Path, output: Path) -> None:
    result = run_torch(torch, "pack", str(source), str(output), "o2r")
    require(
        result.returncode == 0,
        f"Torch pack failed with exit {result.returncode}:\n{result.stdout}\n{result.stderr}",
    )
    require(output.is_file(), f"Torch did not create {output}")


def validate_archive(path: Path, expected_entries: list[str]) -> None:
    with zipfile.ZipFile(path, "r") as archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        require(names == expected_entries, f"unexpected archive order or paths: {names}")
        require(all(not Path(name).is_absolute() for name in names), "archive contains an absolute path")
        require(all(".." not in Path(name).parts for name in names), "archive contains a parent traversal")
        timestamps = {info.date_time for info in infos}
        require(len(timestamps) == 1, f"archive timestamps are not fixed: {sorted(timestamps)}")
        timestamp = next(iter(timestamps))
        require(timestamp == (1980, 1, 1, 0, 0, 0), f"unexpected fixed timestamp: {timestamp}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--torch", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    args = parser.parse_args()

    torch = args.torch.resolve()
    work_dir = args.work_dir.resolve()
    require(torch.is_file(), f"Torch executable is missing: {torch}")

    require(not work_dir.exists(), f"--work-dir must not already exist: {work_dir}")
    source = work_dir / "source"
    (source / "nested" / "deeper").mkdir(parents=True)

    # Deliberately create entries out of lexical order.
    (source / "nested" / "deeper" / "gamma.txt").write_text("gamma\n", encoding="utf-8")
    (source / "alpha.txt").write_text("alpha\n", encoding="utf-8")
    (source / "nested" / "beta.bin").write_bytes(bytes(range(64)))

    inside = source / "inside.o2r"
    contained = run_torch(torch, "pack", str(source), str(inside), "o2r")
    contained_output = contained.stdout + contained.stderr
    require(contained.returncode == 1, "output inside the input tree was accepted")
    require(
        "Output archive must be outside the selected input directory" in contained_output,
        "contained output error was not reported cleanly",
    )
    require(not inside.exists(), "contained output archive was created")

    first = work_dir / "first.o2r"
    second = work_dir / "second.o2r"
    pack(torch, source, first)
    validate_archive(first, BASE_ENTRIES)

    for index, path in enumerate(source.rglob("*")):
        if path.is_file():
            timestamp = 1_700_000_000 + (index * 60)
            os.utime(path, (timestamp, timestamp))

    pack(torch, source, second)
    validate_archive(second, BASE_ENTRIES)
    require(first.read_bytes() == second.read_bytes(), "archives differ after source mtime changes")

    invalid = run_torch(torch, "pack", str(source), str(work_dir / "invalid.bin"), "invalid")
    combined_output = invalid.stdout + invalid.stderr
    require(invalid.returncode == 1, f"invalid archive type returned {invalid.returncode}")
    require("Torch failed:" in combined_output, "invalid archive error was not reported cleanly")

    outside = work_dir / "outside.txt"
    outside.write_text("outside symlink target\n", encoding="utf-8")
    symlink = source / "nested" / "outside-link.txt"
    try:
        symlink.symlink_to(outside)
    except OSError:
        pass
    else:
        escaped = run_torch(torch, "pack", str(source), str(work_dir / "escaped.o2r"), "o2r")
        escaped_output = escaped.stdout + escaped.stderr
        require(escaped.returncode == 1, f"outside symlink returned {escaped.returncode}")
        require("outside the selected input directory" in escaped_output, "outside symlink was not rejected cleanly")

    print(f"deterministic O2R SHA256: {sha256(first)}")
    print("entries: " + ", ".join(BASE_ENTRIES))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
