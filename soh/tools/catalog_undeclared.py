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
    0x4F444C54: "GFX",              # DisplayList
    0x4F4D5458: "MTX",              # Matrix
    0x4F415252: "OOT:ARRAY",        # Array (VTX arrays, generic arrays)
    0x4F424C42: "BLOB",             # Blob (limb tables)
    0x4F534C42: "OOT:LIMB",         # OoTSkeletonLimb
    0x4F434F4C: "OOT:COLLISION",    # OoTCollisionHeader
    0x4F524F4D: "OOT:ROOM",         # OoTRoom
    0x4F565458: "VTX",              # Vertex (rare, most are OOT:ARRAY)
    0x46669697: "LIGHTS",           # Lights
}

# Patterns to extract room/scene name from asset names.
# E.g. "Bmori1_room_0DL_001CB0" → room="Bmori1_room_0"
#      "Bmori1_sceneCollisionHeader_014054" → scene="Bmori1_scene"
ROOM_PATTERN = re.compile(r"^(.+?(?:_room_\d+|_scene))(.*)")

# Skip Set_ variants — these are alternate scene header references to the same
# DList at the same offset. We only want the canonical (non-Set) name.
SET_PATTERN = re.compile(r"Set_[0-9A-Fa-f]+")


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

    if parts[0] == "scenes" and len(parts) == 4:
        mq_status = parts[1]
        asset_name = parts[3]

        # Skip Set_ variants — alternate scene header entries. These are processed
        # at runtime via AddAsset (allowed through for OOT:ROOM/OOT:SCENE).
        if SET_PATTERN.search(asset_name):
            return None, None

        # Extract room/scene name from asset name
        m = ROOM_PATTERN.match(asset_name)
        if m:
            room_name = m.group(1)
            file_key = f"scenes/{mq_status}/{room_name}"
        else:
            # Ambiguous name (e.g. "gMenDL_008118") — can't determine room
            return None, None
        return file_key, asset_name
    else:
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


LIMB_TYPE_MAP = {
    1: "Standard",
    2: "LOD",
    3: "Skin",
    4: "Curve",
    5: "Legacy",
}


def extract_asset_metadata(data, type_name, asset_name):
    """Extract type-specific metadata from the binary resource data."""
    entry = {
        "name": asset_name,
        "type": type_name,
        "symbol": asset_name,
    }

    if type_name == "OOT:ARRAY":
        # Array header at offset 0x40: array_type (uint32), count (uint32)
        if len(data) >= 72:
            arr_type = struct.unpack_from("<I", data, 64)[0]
            count = struct.unpack_from("<I", data, 68)[0]
            entry["count"] = count
            # array_type 25 = VTX
            if arr_type == 25:
                entry["array_type"] = "VTX"

    elif type_name == "OOT:LIMB":
        # Limb header at offset 0x40: limb_type in lower byte of uint32
        if len(data) >= 68:
            limb_type_val = struct.unpack_from("<I", data, 64)[0] & 0xFF
            limb_type_str = LIMB_TYPE_MAP.get(limb_type_val)
            if limb_type_str:
                entry["limb_type"] = limb_type_str

    return entry


def catalog_blob_limb_tables(zf, yaml_dir, declared, assets_by_file, stats):
    """Catalog 0-byte BLOB limb table companion files from O2R.

    These are skeleton limb pointer tables (0 bytes in O2R). The offset is
    computed as: skeleton_offset - (limbCount * 4).
    """
    # Find all *Limbs entries (0-byte, ending in 'Limbs', no 'Limb_' in asset name)
    limb_table_paths = []
    for path in sorted(zf.namelist()):
        parts = path.split("/")
        if len(parts) < 3:
            continue
        asset_name = parts[-1]
        if not asset_name.endswith("Limbs") or "Limb_" in asset_name:
            continue
        data = zf.read(path)
        if len(data) != 0:
            continue
        limb_table_paths.append(path)

    for lt_path in limb_table_paths:
        parts = lt_path.split("/")
        asset_name = parts[-1]
        dir_path = "/".join(parts[:-1])

        # Derive file_key from parse_o2r_path
        file_key, _ = parse_o2r_path(lt_path)
        if file_key is None:
            continue

        if (file_key, asset_name) in declared:
            continue

        # Find parent skeleton: remove "Limbs" suffix
        skel_name = asset_name[:-len("Limbs")]
        skel_path = f"{dir_path}/{skel_name}"
        skel_data = None
        try:
            skel_data = zf.read(skel_path)
        except KeyError:
            pass

        if skel_data is None or len(skel_data) < 0x43:
            stats["skipped_ambiguous"] += 1
            continue

        # Read limb count from skeleton binary (offset 0x42 after 0x40-byte header)
        limb_count = skel_data[0x42]

        # Find skeleton offset from YAML
        skel_offset = None
        yaml_path = os.path.join(yaml_dir, f"{file_key}.yml")
        if os.path.exists(yaml_path):
            with open(yaml_path) as f:
                in_skel = False
                for line in f:
                    stripped = line.rstrip()
                    if stripped == f"{skel_name}:":
                        in_skel = True
                    elif in_skel and stripped.strip().startswith("offset:"):
                        offset_str = stripped.split(":", 1)[1].strip()
                        skel_offset = int(offset_str, 16) if offset_str.startswith("0x") else int(offset_str)
                        break
                    elif in_skel and stripped and not stripped.startswith(" "):
                        break  # next top-level key

        if skel_offset is None:
            stats["skipped_ambiguous"] += 1
            continue

        blob_offset = skel_offset - (limb_count * 4)

        entry = {
            "name": asset_name,
            "type": "BLOB",
            "offset": f"0x{blob_offset:X}",
            "symbol": asset_name,
            "size": 0,
        }

        if file_key not in assets_by_file:
            assets_by_file[file_key] = []
        assets_by_file[file_key].append(entry)
        stats["extracted"] += 1
        stats.setdefault("blob_count", 0)
        stats["blob_count"] += 1


def catalog_mtx_from_dlists(zf, yaml_dir, declared, assets_by_file, stats):
    """Catalog MTX assets by scanning parent DList binaries for G_MTX opcodes.

    MTX names like '{parentDL}Mtx_000000' encode the parent DList but not
    the real offset (which is the G_MTX w1 operand). We walk each parent
    DList's GBI commands to extract w1.
    """
    G_MTX_F3DEX2 = 0x01  # G_MTX opcode for F3DEX2

    # Find all MTX entries in O2R
    mtx_pattern = re.compile(r"^(.+)Mtx_000000$")
    for path in sorted(zf.namelist()):
        file_key, asset_name = parse_o2r_path(path)
        if file_key is None:
            continue

        data = zf.read(path)
        if len(data) < 8:
            continue
        rt = struct.unpack_from("<I", data, 4)[0]
        if rt != 0x4F4D5458:  # MTX
            continue

        if (file_key, asset_name) in declared:
            continue

        m = mtx_pattern.match(asset_name)
        if not m:
            continue

        parent_dl_name = m.group(1)
        # Find parent DList in O2R
        parent_dir = "/".join(path.split("/")[:-1])
        parent_dl_path = f"{parent_dir}/{parent_dl_name}"
        try:
            dl_data = zf.read(parent_dl_path)
        except KeyError:
            stats["skipped_ambiguous"] += 1
            continue

        if len(dl_data) < 0x48:
            stats["skipped_ambiguous"] += 1
            continue

        # Walk DList GBI commands to find G_MTX opcode
        # DList binary: 0x40 header, then 8-byte GBI commands
        mtx_w1 = None
        pos = 0x40
        while pos + 8 <= len(dl_data):
            w0 = struct.unpack_from(">I", dl_data, pos)[0]
            w1 = struct.unpack_from(">I", dl_data, pos + 4)[0]
            opcode = (w0 >> 24) & 0xFF
            if opcode == G_MTX_F3DEX2:
                mtx_w1 = w1
                break
            pos += 8

        if mtx_w1 is None:
            stats["skipped_ambiguous"] += 1
            continue

        # Convert segmented address to file-relative offset
        offset = mtx_w1 & 0x00FFFFFF  # strip segment

        entry = {
            "name": asset_name,
            "type": "MTX",
            "offset": f"0x{offset:X}",
            "symbol": asset_name,
        }

        if file_key not in assets_by_file:
            assets_by_file[file_key] = []
        assets_by_file[file_key].append(entry)
        stats["extracted"] += 1
        stats.setdefault("mtx_count", 0)
        stats["mtx_count"] += 1


def main():
    parser = argparse.ArgumentParser(description="Catalog undeclared assets from reference O2R")
    parser.add_argument("reference_o2r", help="Path to reference O2R file")
    parser.add_argument("yaml_dir", help="Path to YAML directory (to identify already-declared assets)")
    parser.add_argument("output_json", help="Path to output JSON file")
    args = parser.parse_args()

    print(f"Loading declared assets from {args.yaml_dir}...", file=sys.stderr)
    declared = load_yaml_assets(args.yaml_dir)
    print(f"Found {len(declared)} declared assets in YAML", file=sys.stderr)

    assets_by_file = {}
    # Track offsets already seen per file to deduplicate
    seen_offsets = {}  # file_key → set of (type, offset)
    stats = {
        "total_scanned": 0, "already_declared": 0, "extracted": 0,
        "skipped_type": 0, "skipped_set": 0, "skipped_ambiguous": 0,
        "skipped_dup": 0,
    }
    by_type = {}

    with zipfile.ZipFile(args.reference_o2r, "r") as zf:
        for path in sorted(zf.namelist()):
            file_key, asset_name = parse_o2r_path(path)
            if file_key is None:
                if path.startswith("scenes/") and SET_PATTERN.search(path):
                    stats["skipped_set"] += 1
                elif path.startswith("scenes/"):
                    stats["skipped_ambiguous"] += 1
                continue

            # Read resource header to get type
            data = zf.read(path)
            if len(data) < 64:
                continue

            res_type = struct.unpack_from("<I", data, 4)[0]
            type_name = RESOURCE_TYPES.get(res_type)
            if type_name is None:
                stats["skipped_type"] += 1
                continue

            stats["total_scanned"] += 1

            # Check if already declared in YAML
            if (file_key, asset_name) in declared:
                stats["already_declared"] += 1
                continue

            # Extract offset from asset name (last hex segment after DL_, Mtx_, Vtx_, etc.)
            # Or from known patterns in the name
            offset_match = re.search(r"_([0-9A-Fa-f]{4,8})$", asset_name)
            if not offset_match:
                # For assets like "Bmori1_room_0" (OoTRoom) — offset is typically 0x0
                # For collision headers — offset is in the name
                offset_match = re.search(r"(?:Header|Blob)_([0-9A-Fa-f]+)$", asset_name)

            if not offset_match:
                # OoTRoom assets have no offset suffix — they're at offset 0x0
                if type_name == "OOT:ROOM":
                    offset_hex = "0"
                else:
                    continue
            else:
                offset_hex = offset_match.group(1)

            # Deduplicate: skip if we already have this exact asset in this file
            dedup_key = (type_name, asset_name)
            if file_key not in seen_offsets:
                seen_offsets[file_key] = set()
            if dedup_key in seen_offsets[file_key]:
                stats["skipped_dup"] += 1
                continue
            seen_offsets[file_key].add(dedup_key)

            entry = extract_asset_metadata(data, type_name, asset_name)
            entry["offset"] = f"0x{offset_hex.upper()}"

            if file_key not in assets_by_file:
                assets_by_file[file_key] = []
            assets_by_file[file_key].append(entry)
            stats["extracted"] += 1
            by_type[type_name] = by_type.get(type_name, 0) + 1

    # Catalog BLOB limb tables (0-byte companion files)
    with zipfile.ZipFile(args.reference_o2r, "r") as zf:
        catalog_blob_limb_tables(zf, args.yaml_dir, declared, assets_by_file, stats)

    # Sort entries within each file by offset
    for key in assets_by_file:
        assets_by_file[key].sort(key=lambda e: int(e["offset"], 16))

    print(f"\nResults:", file=sys.stderr)
    print(f"  Total scanned: {stats['total_scanned']}", file=sys.stderr)
    print(f"  Already declared: {stats['already_declared']}", file=sys.stderr)
    print(f"  Extracted: {stats['extracted']}", file=sys.stderr)
    print(f"  Skipped (unknown type): {stats['skipped_type']}", file=sys.stderr)
    print(f"  Skipped (Set_ variant): {stats['skipped_set']}", file=sys.stderr)
    print(f"  Skipped (ambiguous scene): {stats['skipped_ambiguous']}", file=sys.stderr)
    print(f"  Skipped (duplicate offset): {stats['skipped_dup']}", file=sys.stderr)
    print(f"  Output files: {len(assets_by_file)}", file=sys.stderr)
    print(f"\n  By type:", file=sys.stderr)
    for t, c in sorted(by_type.items(), key=lambda x: -x[1]):
        print(f"    {t}: {c}", file=sys.stderr)

    with open(args.output_json, "w") as f:
        json.dump(assets_by_file, f, indent=2)
        f.write("\n")

    print(f"\nWrote {args.output_json}", file=sys.stderr)


if __name__ == "__main__":
    main()
