#!/usr/bin/env python3
"""Generate supplemental.json containing all asset data not in ZAPD XML.

Merges data from:
- Reference O2R: VTX arrays, GFX (child DLists), OOT:LIMB, OOT:COLLISION, BLOB
- ROM binary: Set_ alternate headers, MTX offsets from DList walking

Output: Single supplemental.json keyed by DMA names (e.g. "gameplay_keep",
"bdan_room_0"). zapd_to_torch.py maps these to YAML paths.

Usage:
    python3 generate_supplemental.py <reference.o2r> <rom.z64> <dma.json> <output.json>
"""

import argparse
import json
import os
import re
import struct
import sys
import zipfile


# --- Constants ---

RESOURCE_TYPES = {
    0x4F444C54: "GFX",
    0x4F4D5458: "MTX",
    0x4F415252: "OOT:ARRAY",
    0x4F424C42: "BLOB",
    0x4F534C42: "OOT:LIMB",
    0x4F434F4C: "OOT:COLLISION",
    0x4F524F4D: "OOT:ROOM",
    0x4F565458: "VTX",
}

LIMB_TYPE_MAP = {1: "Standard", 2: "LOD", 3: "Skin", 4: "Curve", 5: "Legacy"}

ROOM_PATTERN = re.compile(r"^(.+?(?:_room_\d+|_scene))(.*)")
SET_PATTERN = re.compile(r"Set_[0-9A-Fa-f]+")

# F3DEX2 GBI
G_MTX_F3DEX2 = 0xDA
G_ENDDL = 0xDF
CMD_END_MARKER = 0x14
CMD_SET_ALTERNATE_HEADERS = 0x18
CMD_SET_MESH = 0x0A


# --- Yaz0 ---

def yaz0_decompress(data):
    if data[:4] != b"Yaz0":
        return data
    size = struct.unpack_from(">I", data, 4)[0]
    out = bytearray(size)
    src, dst = 16, 0
    while dst < size and src < len(data):
        code = data[src]; src += 1
        for bit in range(7, -1, -1):
            if dst >= size: break
            if code & (1 << bit):
                out[dst] = data[src]; src += 1; dst += 1
            else:
                if src + 1 >= len(data): break
                b1, b2 = data[src], data[src + 1]; src += 2
                dist = ((b1 & 0x0F) << 8) | b2
                copy_src = dst - dist - 1
                length = b1 >> 4
                if length == 0:
                    if src >= len(data): break
                    length = data[src] + 0x12; src += 1
                else:
                    length += 2
                for _ in range(length):
                    if dst >= size: break
                    out[dst] = out[copy_src]; dst += 1; copy_src += 1
    return bytes(out)


# --- O2R path → DMA name mapping ---

def o2r_path_to_file_key(path):
    """Convert an O2R path to a YAML file key and asset name.

    Returns (file_key, asset_name) or (None, None) if unmappable.
    File key matches the YAML path without .yml extension.
    """
    parts = path.split("/")
    if len(parts) < 3:
        return None, None

    if parts[0] == "scenes" and len(parts) == 4:
        # scenes/nonmq/Bmori1_scene/Bmori1_room_0DL_001CB0
        mq_status = parts[1]  # "shared" or "nonmq"
        asset_name = parts[3]
        # Skip Set_ GFX/MTX aliases (handled by ROM extraction)
        if SET_PATTERN.search(asset_name):
            return None, None
        # Extract room/scene name from asset name
        m = ROOM_PATTERN.match(asset_name)
        if m:
            room_name = m.group(1)
            return f"scenes/{mq_status}/{room_name}", asset_name
        else:
            return None, None  # ambiguous scene name
    elif len(parts) == 3:
        # objects/gameplay_keep/asset, code/sys_matrix/asset
        return f"{parts[0]}/{parts[1]}", parts[2]
    else:
        return None, None


# --- O2R extraction ---

def build_dma_to_path_map(zf):
    """Build DMA name → YAML path mapping from O2R directory structure."""
    dma_map = {}
    for path in zf.namelist():
        parts = path.split("/")
        if parts[0] == "scenes" and len(parts) == 4:
            # scenes/nonmq/Bmori1_scene/asset → extract room/scene DMA name
            asset_name = parts[3]
            m = ROOM_PATTERN.match(asset_name)
            if m:
                dma_name = m.group(1)
                yaml_path = f"scenes/{parts[1]}/{dma_name}"
                dma_map[dma_name] = yaml_path
        elif len(parts) == 3 and parts[0] == "objects":
            dma_map[parts[1]] = f"objects/{parts[1]}"
    return dma_map


def extract_from_o2r(zf):
    """Extract all supplemental asset metadata from reference O2R."""
    assets = {}  # file_key → list of entries
    stats = {"scanned": 0, "extracted": 0, "skipped_set": 0, "skipped_ambiguous": 0}

    for path in sorted(zf.namelist()):
        file_key, asset_name = o2r_path_to_file_key(path)
        if file_key is None:
            if "Set_" in path:
                stats["skipped_set"] += 1
            elif path.startswith("scenes/"):
                stats["skipped_ambiguous"] += 1
            continue

        data = zf.read(path)

        # Handle 0-byte companion files (BLOB limb tables)
        if len(data) == 0 and asset_name.endswith("Limbs") and "Limb_" not in asset_name:
            # BLOB offset computed later from parent skeleton
            if file_key not in assets:
                assets[file_key] = []
            assets[file_key].append({
                "name": asset_name,
                "type": "BLOB",
                "symbol": asset_name,
                "size": 0,
                "_needs_offset": True,  # resolved in post-processing
                "_parent_skel": asset_name[:-len("Limbs")],
            })
            stats["extracted"] += 1
            continue

        if len(data) < 64:
            continue

        res_type = struct.unpack_from("<I", data, 4)[0]
        type_name = RESOURCE_TYPES.get(res_type)
        if type_name is None:
            continue

        stats["scanned"] += 1

        # Skip MTX from O2R — their name-derived offsets are wrong (Mtx_000000
        # is a naming convention, not the real offset). MTX offsets come from ROM.
        if type_name == "MTX":
            continue

        # Extract offset from asset name
        offset_match = re.search(r"_([0-9A-Fa-f]{4,8})$", asset_name)
        if not offset_match:
            offset_match = re.search(r"(?:Header|Blob)_([0-9A-Fa-f]+)$", asset_name)
        if not offset_match:
            if type_name == "OOT:ROOM":
                offset_hex = "0"
            else:
                continue
        else:
            offset_hex = offset_match.group(1)

        entry = {
            "name": asset_name,
            "type": type_name,
            "offset": f"0x{offset_hex.upper()}",
            "symbol": asset_name,
        }

        # Type-specific metadata
        if type_name == "OOT:ARRAY" and len(data) >= 72:
            arr_type = struct.unpack_from("<I", data, 64)[0]
            count = struct.unpack_from("<I", data, 68)[0]
            entry["count"] = count
            if arr_type == 25:
                entry["array_type"] = "VTX"

        elif type_name == "OOT:LIMB" and len(data) >= 68:
            limb_type_val = struct.unpack_from("<I", data, 64)[0] & 0xFF
            limb_type_str = LIMB_TYPE_MAP.get(limb_type_val)
            if limb_type_str:
                entry["limb_type"] = limb_type_str

        if file_key not in assets:
            assets[file_key] = []
        assets[file_key].append(entry)
        stats["extracted"] += 1

    return assets, stats


def resolve_blob_offsets(assets, zf):
    """Resolve BLOB limb table offsets from parent skeleton binaries."""
    resolved = 0
    for file_key, entries in assets.items():
        # Build skeleton offset lookup from same file's entries
        skel_offsets = {}
        skel_limb_counts = {}
        for e in entries:
            if e["type"] == "OOT:SKELETON" if "type" in e else False:
                pass  # skeletons aren't in supplemental — they're in XML

        # Read skeleton binaries from O2R to get limb count
        for e in entries:
            if not e.get("_needs_offset"):
                continue
            parent_name = e["_parent_skel"]
            # Find parent skeleton in O2R
            skel_path = None
            for p in zf.namelist():
                if p.endswith(f"/{parent_name}"):
                    skel_path = p
                    break
            if skel_path is None:
                continue

            skel_data = zf.read(skel_path)
            if len(skel_data) < 0x43:
                continue

            limb_count = skel_data[0x42]

            # Find skeleton offset from the same file's YAML
            # The skeleton is declared in XML, so it's not in our assets dict.
            # We need to find it from the O2R — the skeleton's own path tells us its name,
            # and we can look up its offset from the O2R binary or infer it.
            # The skeleton binary at 0x40 has: skelType(1), limbType(1), limbCount(1), dListCount(1)
            # Then 4 bytes padding, then the limb count again and limb path strings.
            # But we need the skeleton's file offset (from YAML).

            # Alternative: scan the O2R for all entries in same directory to find
            # the skeleton entry and read its offset from the resource.
            # Actually, we can compute: BLOB offset = skeleton offset - (limbCount * 4)
            # But we don't know skeleton offset without the YAML.

            # Simpler: look for the skeleton entry in the same DMA file.
            # If it has an offset-encoded name like "object_ahg_Skel_0000F0", use that.
            skel_offset = None
            skel_offset_match = re.search(r"Skel_([0-9A-Fa-f]+)$", parent_name)
            if skel_offset_match:
                skel_offset = int(skel_offset_match.group(1), 16)
            else:
                # For g-prefix skeletons (gArrowSkel), we need to find the offset
                # from the O2R. Check if there's a skeleton resource we can use.
                # The O2R skeleton binary doesn't store the ROM offset directly.
                # But the YAML will have it. Since supplemental.json is generated
                # alongside the YAML, we can accept that BLOB offsets for g-prefix
                # skeletons need to be filled in by zapd_to_torch (which has the YAML).
                pass

            if skel_offset is not None:
                blob_offset = skel_offset - (limb_count * 4)
                e["offset"] = f"0x{blob_offset:X}"
                del e["_needs_offset"]
                del e["_parent_skel"]
                resolved += 1
            else:
                # Can't resolve — mark with parent info for zapd_to_torch to resolve
                e["_skel_name"] = parent_name
                e["_limb_count"] = limb_count
                del e["_needs_offset"]
                del e["_parent_skel"]

    return resolved


# --- ROM extraction ---

def scan_scene_commands(data, offset):
    """Scan scene/room commands. Returns list of (cmd_id, w0, w1)."""
    commands = []
    pos = offset
    while pos + 8 <= len(data):
        w0 = struct.unpack_from(">I", data, pos)[0]
        w1 = struct.unpack_from(">I", data, pos + 4)[0]
        cmd_id = (w0 >> 24) & 0xFF
        commands.append((cmd_id, w0, w1))
        if cmd_id == CMD_END_MARKER:
            break
        pos += 8
        if pos - offset > 0x1000:
            break
    return commands


def extract_from_rom(rom_data, dma, dma_to_path):
    """Extract Set_ headers and MTX from ROM."""
    assets = {}
    stats = {"set_count": 0, "mtx_count": 0, "files_scanned": 0}

    for dma_name, dma_info in dma.items():
        phys_start = int(dma_info["phys_start"], 16)
        phys_end = int(dma_info.get("phys_end", "0x0"), 16)
        if phys_start == 0:
            continue
        if "_scene" not in dma_name and "_room_" not in dma_name:
            continue

        file_key = dma_to_path.get(dma_name)
        if file_key is None:
            continue

        # Decompress
        if phys_end > phys_start:
            compressed = rom_data[phys_start:phys_end]
        else:
            compressed = rom_data[phys_start:phys_start + 1024 * 1024]
        file_data = yaz0_decompress(compressed)

        seg_num = 2 if "_scene" in dma_name else 3
        stats["files_scanned"] += 1

        commands = scan_scene_commands(file_data, 0)

        # Extract Set_ alternate headers
        for cmd_id, w0, w1 in commands:
            if cmd_id != CMD_SET_ALTERNATE_HEADERS:
                continue
            table_seg = (w1 >> 24) & 0xFF
            table_offset = w1 & 0x00FFFFFF
            if table_seg != seg_num:
                continue

            for i in range(16):
                ptr_addr = table_offset + i * 4
                if ptr_addr + 4 > len(file_data):
                    break
                ptr = struct.unpack_from(">I", file_data, ptr_addr)[0]
                if ptr == 0:
                    continue
                ptr_seg = (ptr >> 24) & 0xFF
                ptr_offset = ptr & 0x00FFFFFF
                if ptr_seg != seg_num:
                    continue

                asset_type = "OOT:ROOM" if "_room_" in dma_name else "OOT:SCENE"
                set_symbol = f"{dma_name}Set_{ptr_offset:06X}"
                entry = {
                    "name": set_symbol,
                    "type": asset_type,
                    "offset": f"0x{ptr_offset:X}",
                    "symbol": set_symbol,
                    "base_name": dma_name,
                }
                if file_key not in assets:
                    assets[file_key] = []
                if not any(e["name"] == set_symbol for e in assets[file_key]):
                    assets[file_key].append(entry)
                    stats["set_count"] += 1
            break

        # Extract MTX from SetMesh DLists
        for cmd_id, w0, w1 in commands:
            if cmd_id != CMD_SET_MESH:
                continue
            mesh_seg = (w1 >> 24) & 0xFF
            mesh_offset = w1 & 0x00FFFFFF
            if mesh_seg != seg_num:
                continue
            if mesh_offset + 12 > len(file_data):
                continue

            mesh_type = file_data[mesh_offset]
            num_entries = file_data[mesh_offset + 1]

            if mesh_type == 0:
                mesh_start = struct.unpack_from(">I", file_data, mesh_offset + 4)[0]
                ms_offset = mesh_start & 0x00FFFFFF
                for j in range(num_entries):
                    entry_pos = ms_offset + j * 8
                    if entry_pos + 8 > len(file_data):
                        break
                    opa_addr = struct.unpack_from(">I", file_data, entry_pos)[0]
                    xlu_addr = struct.unpack_from(">I", file_data, entry_pos + 4)[0]

                    for dl_addr in [opa_addr, xlu_addr]:
                        if dl_addr == 0:
                            continue
                        dl_seg = (dl_addr >> 24) & 0xFF
                        dl_offset = dl_addr & 0x00FFFFFF
                        if dl_seg != seg_num:
                            continue

                        # Walk DList + child DLists for G_MTX
                        dl_queue = [(dl_offset, f"{dma_name}DL_{dl_offset:06X}")]
                        visited = set()
                        while dl_queue:
                            cur_offset, cur_symbol = dl_queue.pop(0)
                            if cur_offset in visited:
                                continue
                            visited.add(cur_offset)
                            pos = cur_offset
                            while pos + 8 <= len(file_data):
                                dw0 = struct.unpack_from(">I", file_data, pos)[0]
                                dw1 = struct.unpack_from(">I", file_data, pos + 4)[0]
                                op = (dw0 >> 24) & 0xFF
                                if op == G_ENDDL:
                                    break
                                if op == G_MTX_F3DEX2 and dw1 != 0:
                                    mtx_seg = (dw1 >> 24) & 0xFF
                                    mtx_offset = dw1 & 0x00FFFFFF
                                    if mtx_seg == seg_num:
                                        mtx_symbol = f"{cur_symbol}Mtx_000000"
                                        entry = {
                                            "name": mtx_symbol,
                                            "type": "MTX",
                                            "offset": f"0x{mtx_offset:X}",
                                            "symbol": mtx_symbol,
                                        }
                                        if file_key not in assets:
                                            assets[file_key] = []
                                        if not any(e["name"] == mtx_symbol for e in assets[file_key]):
                                            assets[file_key].append(entry)
                                            stats["mtx_count"] += 1
                                # Follow G_DL child display lists
                                G_DL_F3DEX2 = 0xDE
                                if op == G_DL_F3DEX2 and dw1 != 0:
                                    child_seg = (dw1 >> 24) & 0xFF
                                    child_offset = dw1 & 0x00FFFFFF
                                    if child_seg == seg_num:
                                        child_symbol = f"{dma_name}DL_{child_offset:06X}"
                                        dl_queue.append((child_offset, child_symbol))
                                pos += 8
            break

    return assets, stats


# --- Main ---

def main():
    parser = argparse.ArgumentParser(description="Generate supplemental.json for YAML enrichment")
    parser.add_argument("reference_o2r", help="Path to reference O2R file")
    parser.add_argument("rom", help="Path to ROM file (.z64)")
    parser.add_argument("dma_json", help="Path to DMA table JSON")
    parser.add_argument("output_json", help="Path to output supplemental JSON")
    args = parser.parse_args()

    print("Reading ROM...", file=sys.stderr)
    with open(args.rom, "rb") as f:
        rom_data = f.read()

    with open(args.dma_json) as f:
        dma = json.load(f)

    # Step 1: Extract from reference O2R
    print("Extracting from reference O2R...", file=sys.stderr)
    with zipfile.ZipFile(args.reference_o2r, "r") as zf:
        dma_to_path = build_dma_to_path_map(zf)
        o2r_assets, o2r_stats = extract_from_o2r(zf)
        blob_resolved = resolve_blob_offsets(o2r_assets, zf)

    print(f"  O2R: {o2r_stats['extracted']} assets, {o2r_stats['skipped_set']} Set_ skipped, "
          f"{blob_resolved} BLOB offsets resolved", file=sys.stderr)
    print(f"  DMA→path mappings: {len(dma_to_path)}", file=sys.stderr)

    # Step 2: Extract from ROM (Set_ and MTX)
    # Note: Set_ entries are currently excluded — pre-declaring them causes Torch to
    # process them in the wrong context (without proper segment decompression).
    # They continue to be created via AddAsset at runtime.
    print("Extracting from ROM...", file=sys.stderr)
    rom_assets, rom_stats = extract_from_rom(rom_data, dma, dma_to_path)
    # Remove Set_ entries for now
    for fk in list(rom_assets.keys()):
        rom_assets[fk] = [e for e in rom_assets[fk] if "Set_" not in e.get("name", "")]
        if not rom_assets[fk]:
            del rom_assets[fk]
    rom_stats["set_count"] = 0
    print(f"  ROM: {rom_stats['set_count']} Set_ headers, {rom_stats['mtx_count']} MTX, "
          f"{rom_stats['files_scanned']} files scanned", file=sys.stderr)

    # Step 3: Merge (ROM data takes precedence for same file_key)
    merged = {}
    for file_key, entries in o2r_assets.items():
        if file_key not in merged:
            merged[file_key] = []
        merged[file_key].extend(entries)

    for file_key, entries in rom_assets.items():
        if file_key not in merged:
            merged[file_key] = []
        existing_names = {e["name"] for e in merged[file_key]}
        for e in entries:
            if e["name"] not in existing_names:
                merged[file_key].append(e)

    # Keep unresolved BLOBs (with _skel_name/_limb_count) — zapd_to_torch resolves them
    # Filter out entries missing both offset AND resolution fields
    for key in list(merged.keys()):
        merged[key] = [e for e in merged[key] if "offset" in e or "_skel_name" in e]
        if not merged[key]:
            del merged[key]

    # Sort entries within each file (entries without offset go at end)
    for key in merged:
        merged[key].sort(key=lambda e: int(e["offset"], 16) if "offset" in e else 0xFFFFFFFF)

    # Summary
    from collections import Counter
    types = Counter()
    for entries in merged.values():
        for e in entries:
            types[e["type"]] += 1
    total = sum(types.values())
    print(f"\nTotal: {total} assets across {len(merged)} files", file=sys.stderr)
    for t, c in types.most_common():
        print(f"  {t}: {c}", file=sys.stderr)

    with open(args.output_json, "w") as f:
        json.dump(merged, f, indent=2)
        f.write("\n")

    print(f"\nWrote {args.output_json}", file=sys.stderr)


if __name__ == "__main__":
    main()
