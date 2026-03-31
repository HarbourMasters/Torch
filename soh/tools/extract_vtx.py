#!/usr/bin/env python3
"""Extract VTX entry metadata from a reference O2R file.

Scans the O2R zip for VTX assets (matching *Vtx_HEXOFFSET pattern),
reads their vertex counts from the binary data, and outputs a JSON
file grouped by source file for use by zapd_to_torch.py.

Usage:
    python3 extract_vtx.py <reference.o2r> <output.json>
"""

import argparse
import json
import re
import struct
import sys
import zipfile


def main():
    parser = argparse.ArgumentParser(description="Extract VTX metadata from reference O2R")
    parser.add_argument("reference_o2r", help="Path to reference O2R file")
    parser.add_argument("output_json", help="Path to output JSON file")
    args = parser.parse_args()

    vtx_pattern = re.compile(r"^(.+?)/(.+?)/(.*Vtx_([0-9A-Fa-f]+))$")
    vtx_by_file = {}
    skipped = 0

    with zipfile.ZipFile(args.reference_o2r, "r") as zf:
        for path in sorted(zf.namelist()):
            m = vtx_pattern.match(path)
            if not m:
                continue

            category, filename, asset_name, offset_hex = m.groups()
            offset = int(offset_hex, 16)

            data = zf.read(path)
            if len(data) < 72:
                print(f"  SKIP (too small): {path}", file=sys.stderr)
                skipped += 1
                continue

            arr_type = struct.unpack_from("<I", data, 64)[0]
            count = struct.unpack_from("<I", data, 68)[0]

            if arr_type != 25:
                print(f"  SKIP (not vertex array, type={arr_type}): {path}", file=sys.stderr)
                skipped += 1
                continue

            key = f"{category}/{filename}"
            if key not in vtx_by_file:
                vtx_by_file[key] = []
            vtx_by_file[key].append({
                "name": asset_name,
                "offset": f"0x{offset:X}",
                "count": count,
            })

    # Sort entries within each file by offset
    for key in vtx_by_file:
        vtx_by_file[key].sort(key=lambda e: int(e["offset"], 16))

    total = sum(len(v) for v in vtx_by_file.values())
    print(f"Extracted {total} VTX entries across {len(vtx_by_file)} files ({skipped} skipped)")

    with open(args.output_json, "w") as f:
        json.dump(vtx_by_file, f, indent=2)
        f.write("\n")

    print(f"Wrote {args.output_json}")


if __name__ == "__main__":
    main()
