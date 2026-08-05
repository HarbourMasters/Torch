#!/usr/bin/env python3

import argparse
from pathlib import Path
import zipfile


FIXED_TIMESTAMP = (2000, 1, 1, 0, 0, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Package libultraship shaders reproducibly.")
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    source = args.source.resolve()
    shaders = source / "shaders"
    files = sorted(path for path in shaders.rglob("*") if path.is_file())
    if not files:
        parser.error(f"no shader files found below {shaders}")

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")

    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_STORED) as archive:
            for path in files:
                name = path.relative_to(source).as_posix()
                entry = zipfile.ZipInfo(name, FIXED_TIMESTAMP)
                entry.create_system = 3
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, path.read_bytes())
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
