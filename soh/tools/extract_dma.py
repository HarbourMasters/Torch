#!/usr/bin/env python3
"""Extract DMA tables from OoT ROMs and write JSON files.

Usage: python3 extract_dma.py [rom_dir] [out_dir]

Reads all identified ROM files from rom_dir (default: soh/roms/),
extracts DMA tables using the appropriate filelist and offset,
and writes JSON files to out_dir (default: soh/dma/).

Each JSON file maps filename -> {virt_start, virt_end, phys_start, phys_end}
with hex string values.
"""

import json
import os
import struct
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SOH_DIR = os.path.dirname(SCRIPT_DIR)
SHIPWRIGHT_FILELISTS = os.path.join(
    os.path.dirname(os.path.dirname(SOH_DIR)),
    "Shipwright", "soh", "assets", "extractor", "filelists"
)

# Map dir_name prefix -> (filelist, dma_offset)
# Dir names from identify_roms.sh: <dir_name>_<6 char sha>.z64
ROM_VERSIONS = {
    "ntsc_j_1-0":           ("ntsc_oot.txt",     0x7430),
    "ntsc_j_1-1":           ("ntsc_oot.txt",     0x7430),
    "ntsc_j_1-2":           ("ntsc_12_oot.txt",  0x7430),
    "ntsc_u_1-0":           ("ntsc_oot.txt",     0x7430),
    "ntsc_u_1-1":           ("ntsc_oot.txt",     0x7430),
    "ntsc_u_1-2":           ("ntsc_12_oot.txt",  0x7430),
    "ntsc_u_gc":            ("gamecube.txt",     0x7170),
    "ntsc_u_mq":            ("gamecube.txt",     0x7170),
    "ntsc_j_gc":            ("gamecube.txt",     0x7170),
    "ntsc_j_mq":            ("gamecube.txt",     0x7170),
    "ntsc_j_gc_collection": ("gamecube.txt",     0x7170),
    "pal_1-0":              ("pal_oot.txt",      0x7950),
    "pal_1-1":              ("pal_oot.txt",      0x7950),
    "pal_gc":               ("gamecube_pal.txt", 0x7170),
    "pal_mq":               ("gamecube_pal.txt", 0x7170),
    "pal_gc_dbg":           ("dbg.txt",          0x12F70),
    "pal_mq_dbg":           ("dbg.txt",          0x12F70),
}


def load_filelist(filelist_path):
    with open(filelist_path) as f:
        return [line.strip() for line in f]


def extract_dma_table(rom_path, filelist, dma_offset):
    with open(rom_path, "rb") as f:
        rom_data = f.read()

    entries = {}
    for i, name in enumerate(filelist):
        off = dma_offset + 16 * i
        if off + 16 > len(rom_data):
            break
        virt_start, virt_end, phys_start, phys_end = struct.unpack_from(">IIII", rom_data, off)

        # DMA table ends with an all-zero entry (after valid entries)
        # but some entries legitimately have phys_start=0 (first file), so check name
        if virt_start == 0 and virt_end == 0 and phys_start == 0 and phys_end == 0 and i > 0:
            # Could be padding at end; skip but don't break (some tables have gaps)
            continue

        entries[name] = {
            "virt_start": f"0x{virt_start:08X}",
            "virt_end": f"0x{virt_end:08X}",
            "phys_start": f"0x{phys_start:08X}",
            "phys_end": f"0x{phys_end:08X}",
        }

    return entries


def dir_name_from_filename(filename):
    """Extract dir_name from 'dir_name_XXXXXX.z64' format."""
    # Remove .z64 extension, then strip last 7 chars (_XXXXXX)
    base = filename.rsplit(".", 1)[0]
    return base.rsplit("_", 1)[0]


def main():
    rom_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SOH_DIR, "roms")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(SOH_DIR, "dma")

    os.makedirs(out_dir, exist_ok=True)

    # Cache loaded filelists
    filelist_cache = {}

    rom_files = sorted(f for f in os.listdir(rom_dir) if f.endswith(".z64"))
    processed = 0
    skipped = 0

    for rom_file in rom_files:
        dir_name = dir_name_from_filename(rom_file)

        if dir_name not in ROM_VERSIONS:
            print(f"  SKIP    {rom_file} (unknown version: {dir_name})")
            skipped += 1
            continue

        filelist_name, dma_offset = ROM_VERSIONS[dir_name]
        filelist_path = os.path.join(SHIPWRIGHT_FILELISTS, filelist_name)

        if filelist_name not in filelist_cache:
            if not os.path.exists(filelist_path):
                print(f"  ERROR   {rom_file} — filelist not found: {filelist_path}")
                skipped += 1
                continue
            filelist_cache[filelist_name] = load_filelist(filelist_path)

        filelist = filelist_cache[filelist_name]
        rom_path = os.path.join(rom_dir, rom_file)

        entries = extract_dma_table(rom_path, filelist, dma_offset)

        out_path = os.path.join(out_dir, f"{dir_name}.json")
        with open(out_path, "w") as f:
            json.dump(entries, f, indent=2)
            f.write("\n")

        compressed = sum(1 for e in entries.values() if e["phys_end"] != "0x00000000")
        print(f"  OK      {rom_file} → {dir_name}.json ({len(entries)} entries, {compressed} compressed)")
        processed += 1

    print()
    print(f"--- Summary ---")
    print(f"Processed: {processed}")
    print(f"Skipped:   {skipped}")


if __name__ == "__main__":
    main()
