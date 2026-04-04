#!/usr/bin/env python3
"""Extract asset metadata from ROM that can't be derived from the reference O2R.

Reads the ROM binary + DMA table to discover:
- MTX offsets (by scanning scene/room segments for G_MTX opcodes in DLists)
- Set_ alternate scene/room headers (by reading SetAlternateHeaders commands)

Only needs ROM + DMA JSON — no YAML or reference O2R required.

Usage:
    python3 extract_rom_assets.py <rom.z64> <dma.json> <output.json>
"""

import argparse
import json
import os
import re
import struct
import sys


def yaz0_decompress(data):
    """Decompress Yaz0-encoded data."""
    if data[:4] != b"Yaz0":
        return data  # not compressed
    decompressed_size = struct.unpack_from(">I", data, 4)[0]
    out = bytearray(decompressed_size)
    src = 16  # skip header
    dst = 0
    while dst < decompressed_size and src < len(data):
        code_byte = data[src]
        src += 1
        for bit in range(7, -1, -1):
            if dst >= decompressed_size:
                break
            if code_byte & (1 << bit):
                out[dst] = data[src]
                src += 1
                dst += 1
            else:
                if src + 1 >= len(data):
                    break
                b1 = data[src]
                b2 = data[src + 1]
                src += 2
                dist = ((b1 & 0x0F) << 8) | b2
                copy_src = dst - dist - 1
                length = b1 >> 4
                if length == 0:
                    if src >= len(data):
                        break
                    length = data[src] + 0x12
                    src += 1
                else:
                    length += 2
                for _ in range(length):
                    if dst >= decompressed_size:
                        break
                    out[dst] = out[copy_src]
                    dst += 1
                    copy_src += 1
    return bytes(out)


# F3DEX2 GBI opcodes
G_MTX_F3DEX2 = 0xDA
G_VTX_F3DEX2 = 0x01
G_DL_F3DEX2 = 0xDE
G_ENDDL = 0xDF
G_RDPHALF_1 = 0xE1

# Scene command IDs
CMD_END_MARKER = 0x14
CMD_SET_ALTERNATE_HEADERS = 0x18
CMD_SET_MESH = 0x0A


def read_rom(rom_path):
    with open(rom_path, "rb") as f:
        return f.read()


def load_dma(dma_path):
    with open(dma_path) as f:
        return json.load(f)


def is_scene_or_room(name):
    """Check if a DMA entry is a scene or room file."""
    # Scene files end with _scene, room files have _room_ in them
    # DMA names: "bdan_room_0", "bdan_scene", "spot00_scene", etc.
    return "_scene" in name or "_room_" in name


def get_dma_category(name):
    """Get the category path for a DMA entry (scenes/shared, scenes/nonmq, objects, etc.)."""
    # This matches the YAML directory structure
    if "_scene" in name or "_room_" in name:
        return "scenes"  # subcategory determined by content
    return None


def scan_scene_commands(rom_data, rom_start, seg_num):
    """Scan scene/room commands starting at rom_start. Returns list of commands."""
    commands = []
    pos = rom_start
    while pos + 8 <= len(rom_data):
        w0 = struct.unpack_from(">I", rom_data, pos)[0]
        w1 = struct.unpack_from(">I", rom_data, pos + 4)[0]
        cmd_id = (w0 >> 24) & 0xFF
        commands.append((cmd_id, w0, w1, pos))
        if cmd_id == CMD_END_MARKER:
            break
        pos += 8
        if pos - rom_start > 0x1000:  # safety limit
            break
    return commands


def scan_dlist_for_mtx(file_data, dl_pos, seg_num, dl_name, max_cmds=2000):
    """Walk a DList's GBI commands looking for G_MTX. Returns list of (symbol, offset)."""
    mtx_entries = []
    pos = dl_pos
    cmds = 0
    while pos + 8 <= len(file_data) and cmds < max_cmds:
        w0 = struct.unpack_from(">I", file_data, pos)[0]
        w1 = struct.unpack_from(">I", file_data, pos + 4)[0]
        opcode = (w0 >> 24) & 0xFF

        if opcode == G_ENDDL:
            break

        if opcode == G_MTX_F3DEX2 and w1 != 0:
            mtx_seg = (w1 >> 24) & 0xFF
            mtx_offset = w1 & 0x00FFFFFF
            if mtx_seg == seg_num:
                symbol = f"{dl_name}Mtx_000000"
                mtx_entries.append((symbol, mtx_offset))

        pos += 8
        cmds += 1
    return mtx_entries


def extract_set_headers(decompressed_files, scene_room_entries):
    """Extract Set_ alternate header entries from scenes and rooms."""
    set_entries = {}

    for entry_name, data_offset, seg_num, file_key in scene_room_entries:
        file_data = decompressed_files.get(entry_name)
        if file_data is None:
            continue
        commands = scan_scene_commands(file_data, data_offset, seg_num)

        for cmd_id, w0, w1, cmd_pos in commands:
            if cmd_id != CMD_SET_ALTERNATE_HEADERS:
                continue

            table_seg = (w1 >> 24) & 0xFF
            table_offset = w1 & 0x00FFFFFF
            if table_seg != seg_num:
                continue

            # data_offset is 0 (start of decompressed file), table_offset is segment-relative
            table_rom = data_offset + table_offset

            # Hmm, that's only correct if the scene/room entry is at offset 0 in the segment.
            # For scenes, the scene is always at offset 0 of its segment.
            # But the table_offset is a segmented offset from the segment base.
            # Since rom_start = segment_base + 0, table_rom = rom_start + table_offset. ✓

            for i in range(16):
                ptr_addr = table_rom + i * 4
                if ptr_addr + 4 > len(file_data):
                    break
                ptr = struct.unpack_from(">I", file_data, ptr_addr)[0]
                if ptr == 0:
                    continue

                ptr_seg = (ptr >> 24) & 0xFF
                ptr_offset = ptr & 0x00FFFFFF
                if ptr_seg != seg_num:
                    continue

                asset_type = "OOT:ROOM" if "_room_" in entry_name else "OOT:SCENE"
                set_symbol = f"{entry_name}Set_{ptr_offset:06X}"

                entry = {
                    "name": set_symbol,
                    "type": asset_type,
                    "offset": f"0x{ptr_offset:X}",
                    "symbol": set_symbol,
                    "base_name": entry_name,
                }

                if file_key not in set_entries:
                    set_entries[file_key] = []
                if not any(e["name"] == set_symbol for e in set_entries[file_key]):
                    set_entries[file_key].append(entry)

            break  # only one SetAlternateHeaders per scene/room

    return set_entries


def extract_mtx_from_scenes(decompressed_files, scene_room_entries):
    """Scan scene/room DLists for G_MTX references."""
    mtx_entries = {}

    for entry_name, data_offset, seg_num, file_key in scene_room_entries:
        file_data = decompressed_files.get(entry_name)
        if file_data is None:
            continue
        commands = scan_scene_commands(file_data, data_offset, seg_num)

        # Find SetMesh command to get DList addresses
        for cmd_id, w0, w1, cmd_pos in commands:
            if cmd_id != CMD_SET_MESH:
                continue

            mesh_seg = (w1 >> 24) & 0xFF
            mesh_offset = w1 & 0x00FFFFFF
            if mesh_seg != seg_num:
                continue

            mesh_pos = data_offset + mesh_offset
            if mesh_pos + 12 > len(file_data):
                continue

            mesh_type = file_data[mesh_pos]
            num_entries = file_data[mesh_pos + 1]

            if mesh_type == 0:
                # Type 0: array of PolygonDList entries (opa + xlu pairs)
                mesh_start = struct.unpack_from(">I", file_data, mesh_pos + 4)[0]
                ms_offset = mesh_start & 0x00FFFFFF
                for j in range(num_entries):
                    entry_pos = data_offset + ms_offset + j * 8
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

                        dl_pos = data_offset + dl_offset
                        dl_symbol = f"{entry_name}DL_{dl_offset:06X}"
                        mtx_found = scan_dlist_for_mtx(file_data, dl_pos, seg_num, dl_symbol)
                        for symbol, offset in mtx_found:
                            entry = {
                                "name": symbol,
                                "type": "MTX",
                                "offset": f"0x{offset:X}",
                                "symbol": symbol,
                            }
                            if file_key not in mtx_entries:
                                mtx_entries[file_key] = []
                            if not any(e["name"] == symbol for e in mtx_entries[file_key]):
                                mtx_entries[file_key].append(entry)

            break  # only one SetMesh per scene/room

    return mtx_entries


def main():
    parser = argparse.ArgumentParser(description="Extract asset metadata from ROM")
    parser.add_argument("rom", help="Path to ROM file (.z64)")
    parser.add_argument("dma_json", help="Path to DMA table JSON")
    parser.add_argument("output_json", help="Path to output JSON file")
    args = parser.parse_args()

    print(f"Reading ROM...", file=sys.stderr)
    rom_data = read_rom(args.rom)
    print(f"ROM size: {len(rom_data)} bytes", file=sys.stderr)

    dma = load_dma(args.dma_json)

    # Build list of scene/room entries with their ROM addresses
    # DMA entries map file names to ROM offsets
    scene_room_entries = []  # (entry_name, rom_start, seg_num, file_key)

    # Decompress and cache scene/room files
    decompressed_files = {}  # dma_name → decompressed bytes

    for dma_name, dma_info in dma.items():
        phys_start = int(dma_info["phys_start"], 16)
        phys_end = int(dma_info.get("phys_end", "0x0"), 16)
        if phys_start == 0 or not is_scene_or_room(dma_name):
            continue

        # Read and decompress
        if phys_end > phys_start:
            compressed = rom_data[phys_start:phys_end]
        else:
            # Estimate size — read up to 1MB
            compressed = rom_data[phys_start:phys_start + 1024 * 1024]
        file_data = yaz0_decompress(compressed)
        decompressed_files[dma_name] = file_data

        # For scenes: segment is typically 2, for rooms: 3
        if "_scene" in dma_name:
            seg_num = 2
        else:
            seg_num = 3

        # rom_start=0 since we're working with decompressed data directly
        scene_room_entries.append((dma_name, 0, seg_num, dma_name))

    print(f"Found {len(scene_room_entries)} scene/room DMA entries ({len(decompressed_files)} decompressed)", file=sys.stderr)

    assets_by_file = {}

    # Process each decompressed file
    print(f"Extracting Set_ alternate headers...", file=sys.stderr)
    sets = extract_set_headers(decompressed_files, scene_room_entries)
    set_count = sum(len(v) for v in sets.values())
    print(f"  Found {set_count} Set_ entries", file=sys.stderr)
    for k, entries in sets.items():
        if k not in assets_by_file:
            assets_by_file[k] = []
        assets_by_file[k].extend(entries)

    print(f"Extracting MTX from scene DLists...", file=sys.stderr)
    mtx = extract_mtx_from_scenes(decompressed_files, scene_room_entries)
    mtx_count = sum(len(v) for v in mtx.values())
    print(f"  Found {mtx_count} MTX entries", file=sys.stderr)
    for k, entries in mtx.items():
        if k not in assets_by_file:
            assets_by_file[k] = []
        assets_by_file[k].extend(entries)

    # Sort entries
    for key in assets_by_file:
        assets_by_file[key].sort(key=lambda e: int(e["offset"], 16))

    total = sum(len(v) for v in assets_by_file.values())
    print(f"\nTotal: {total} assets across {len(assets_by_file)} files", file=sys.stderr)

    with open(args.output_json, "w") as f:
        json.dump(assets_by_file, f, indent=2)
        f.write("\n")

    print(f"Wrote {args.output_json}", file=sys.stderr)


if __name__ == "__main__":
    main()
