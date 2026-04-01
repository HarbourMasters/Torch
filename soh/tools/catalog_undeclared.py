#!/usr/bin/env python3
"""Catalog undeclared assets by comparing a reference O2R against YAML.

Scans the O2R zip for assets that exist in the archive but are not declared
in the YAML files (i.e. they were auto-discovered by Torch at runtime).
Outputs a JSON file grouped by source file for use by zapd_to_torch.py
to enrich the YAML with pre-declarations.

Usage:
    python3 catalog_undeclared.py <reference.o2r> <yaml_dir> <output.json>

The yaml_dir is used to determine which assets are already declared (and thus
don't need to be extracted). Only assets present in the O2R but missing from
the YAML are included in the output.
"""

import argparse
import json
import os
import re
import struct
import sys
import zipfile


# Resource type IDs from ResourceType.h (little-endian uint32 at offset 0x04)
RESOURCE_TYPES = {
    0x4F444C54: "GFX",         # DisplayList
    0x4F4D5458: "MTX",         # Matrix
    0x46669697: "LIGHTS",      # Lights
}

# Patterns for asset names that indicate auto-discovered assets.
# These match the naming convention used by Torch's AddAsset:
#   DL_HEXOFFSET, Mtx_HEXOFFSET, Lights_HEXOFFSET
ASSET_PATTERNS = {
    "GFX": re.compile(r".*DL_([0-9A-Fa-f]{6})$"),
    "MTX": re.compile(r".*Mtx_([0-9A-Fa-f]{6})$"),
    "LIGHTS": re.compile(r".*Lights_([0-9A-Fa-f]+)$"),
}

# Patterns to extract room/scene name from auto-discovered asset names.
# E.g. "Bmori1_room_0DL_001CB0" → room="Bmori1_room_0", suffix="DL_001CB0"
ROOM_PATTERN = re.compile(r"^(.+?(?:_room_\d+|_scene))(.*)$")


def parse_o2r_path(path):
    """Parse an O2R path into (file_key, asset_name).

    Returns the file_key that maps to a YAML file path (without .yml extension)
    and the asset_name within that file.

    O2R paths come in two forms:
      objects/filename/asset_name       → file_key = objects/filename
      scenes/mq/scene_name/asset_name  → file_key = scenes/mq/room_or_scene_name
        where room_or_scene_name is extracted from the asset_name prefix
    """
    parts = path.split("/")
    if len(parts) < 3:
        return None, None

    if parts[0] == "scenes":
        # Scene assets are tightly coupled with runtime segment context.
        # Pre-declaring them changes how rooms get processed, causing binary mismatches.
        # Skip all scene assets; Torch's auto-discovery handles them correctly.
        return None, None
    else:
        # objects/gameplay_keep/assetDL_001234
        # First two parts are the file key, rest is asset name
        file_key = f"{parts[0]}/{parts[1]}"
        asset_name = "/".join(parts[2:])
        return file_key, asset_name


def load_yaml_assets(yaml_dir):
    """Load all declared asset names, keyed by (file_key, asset_name)."""
    declared = set()
    if not os.path.isdir(yaml_dir):
        return declared

    for root, dirs, files in os.walk(yaml_dir):
        for fname in files:
            if not (fname.endswith(".yml") or fname.endswith(".yaml")):
                continue
            fpath = os.path.join(root, fname)
            rel = os.path.relpath(fpath, yaml_dir)
            rel_noext = os.path.splitext(rel)[0]
            with open(fpath) as f:
                for line in f:
                    stripped = line.rstrip()
                    if stripped and not stripped.startswith(" ") and stripped.endswith(":") and stripped != ":config:":
                        asset_name = stripped[:-1]
                        declared.add((rel_noext, asset_name))
    return declared


def main():
    parser = argparse.ArgumentParser(description="Extract auto-discovered asset metadata from reference O2R")
    parser.add_argument("reference_o2r", help="Path to reference O2R file")
    parser.add_argument("yaml_dir", help="Path to YAML directory (to identify already-declared assets)")
    parser.add_argument("output_json", help="Path to output JSON file")
    args = parser.parse_args()

    print(f"Loading declared assets from {args.yaml_dir}...", file=sys.stderr)
    declared = load_yaml_assets(args.yaml_dir)
    print(f"Found {len(declared)} declared assets in YAML", file=sys.stderr)

    assets_by_file = {}
    stats = {"total_scanned": 0, "already_declared": 0, "extracted": 0, "skipped_no_match": 0}

    with zipfile.ZipFile(args.reference_o2r, "r") as zf:
        for path in sorted(zf.namelist()):
            file_key, asset_name = parse_o2r_path(path)
            if file_key is None:
                continue

            # Read resource header to get type
            data = zf.read(path)
            if len(data) < 64:
                continue

            res_type = struct.unpack_from("<I", data, 4)[0]
            type_name = RESOURCE_TYPES.get(res_type)
            if type_name is None:
                continue

            stats["total_scanned"] += 1

            # Check if this asset matches an auto-discovery pattern
            pattern = ASSET_PATTERNS.get(type_name)
            if pattern is None:
                stats["skipped_no_match"] += 1
                continue

            match = pattern.match(asset_name)
            if not match:
                stats["skipped_no_match"] += 1
                continue

            # Check if already declared in YAML
            if (file_key, asset_name) in declared:
                stats["already_declared"] += 1
                continue

            offset_hex = match.group(1)

            entry = {
                "name": asset_name,
                "type": type_name,
                "offset": f"0x{offset_hex.upper()}",
                "symbol": asset_name,
            }

            if file_key not in assets_by_file:
                assets_by_file[file_key] = []
            assets_by_file[file_key].append(entry)
            stats["extracted"] += 1

    # Sort entries within each file by offset
    for key in assets_by_file:
        assets_by_file[key].sort(key=lambda e: int(e["offset"], 16))

    print(f"\nResults:", file=sys.stderr)
    print(f"  Total matching assets scanned: {stats['total_scanned']}", file=sys.stderr)
    print(f"  Already declared in YAML: {stats['already_declared']}", file=sys.stderr)
    print(f"  Extracted (auto-discovered): {stats['extracted']}", file=sys.stderr)
    print(f"  Skipped (no pattern match): {stats['skipped_no_match']}", file=sys.stderr)
    print(f"  Output files: {len(assets_by_file)}", file=sys.stderr)

    with open(args.output_json, "w") as f:
        json.dump(assets_by_file, f, indent=2)
        f.write("\n")

    print(f"\nWrote {args.output_json}", file=sys.stderr)


if __name__ == "__main__":
    main()
