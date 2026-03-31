#!/usr/bin/env python3
"""Compare a single asset between two O2R files.

Usage:
    python3 compare_asset.py <reference.o2r> <generated.o2r> <asset-path>

Example:
    python3 soh/tools/compare_asset.py soh/o2r/reference.o2r oot.o2r objects/object_firefly/gKeeseSkeletonLimbsLimb_001744
"""

import hashlib
import os
import sys
import zipfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SOH_DIR = os.path.dirname(SCRIPT_DIR)
DEFAULT_REF = os.path.join(SOH_DIR, "o2r", "reference", "pal_gc_0227d7.o2r")
DEFAULT_GEN = os.path.join(SOH_DIR, "o2r", "generated.o2r")


def hexdump(data, width=16):
    lines = []
    for i in range(0, len(data), width):
        chunk = data[i:i+width]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  {i:04x}: {hex_part:<{width*3}}  {ascii_part}")
    return lines


def main():
    if len(sys.argv) < 2:
        print("Usage: compare_asset.py <asset-path> [reference.o2r] [generated.o2r]")
        sys.exit(1)

    asset = sys.argv[1]
    ref_o2r = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_REF
    gen_o2r = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_GEN

    ref_data = gen_data = None

    with zipfile.ZipFile(ref_o2r) as zf:
        if asset in zf.namelist():
            ref_data = zf.read(asset)
        else:
            print(f"NOT FOUND in reference: {asset}")

    with zipfile.ZipFile(gen_o2r) as zf:
        if asset in zf.namelist():
            gen_data = zf.read(asset)
        else:
            print(f"NOT FOUND in generated: {asset}")

    if ref_data is None and gen_data is None:
        sys.exit(1)

    if ref_data is not None and gen_data is not None:
        ref_hash = hashlib.sha256(ref_data).hexdigest()
        gen_hash = hashlib.sha256(gen_data).hexdigest()
        if ref_hash == gen_hash:
            print(f"PASS {asset} ({len(ref_data)} bytes)")
            sys.exit(0)

        print(f"FAIL {asset}")
        print(f"  ref: {len(ref_data)} bytes  sha256: {ref_hash}")
        print(f"  gen: {len(gen_data)} bytes  sha256: {gen_hash}")
        print()

    if ref_data is not None:
        print(f"=== REF ({len(ref_data)} bytes) ===")
        for line in hexdump(ref_data):
            print(line)
        print()

    if gen_data is not None:
        print(f"=== GEN ({len(gen_data)} bytes) ===")
        for line in hexdump(gen_data):
            print(line)
        print()

    if ref_data is not None and gen_data is not None:
        print("=== DIFF ===")
        ref_lines = hexdump(ref_data)
        gen_lines = hexdump(gen_data)
        max_lines = max(len(ref_lines), len(gen_lines))
        for i in range(max_lines):
            r = ref_lines[i] if i < len(ref_lines) else "  (eof)"
            g = gen_lines[i] if i < len(gen_lines) else "  (eof)"
            if r != g:
                print(f"  REF: {r.strip()}")
                print(f"  GEN: {g.strip()}")
                print()

    sys.exit(1)


if __name__ == "__main__":
    main()
