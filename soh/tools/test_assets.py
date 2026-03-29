#!/usr/bin/env python3
"""Test OoT assets by comparing Torch output against a reference O2R manifest.

Usage:
    python3 soh/tools/test_assets.py <rom> [options]

Examples:
    python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --category objects
    python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --file gameplay_keep
    python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --category objects --type GFX --failures-only
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

try:
    import yaml
except ImportError:
    yaml = None

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SOH_DIR = os.path.dirname(SCRIPT_DIR)
MAIN_DIR = os.path.join(SOH_DIR, "assets", "yml")
LOG_DIR = os.path.join(SOH_DIR, "logs")
TORCH = os.path.join(SOH_DIR, "..", "build", "torch")


def elapsed(t0):
    return f"[{time.time() - t0:.1f}s]"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_asset_path(path):
    """Split asset path into (category, file, asset_name)."""
    parts = path.split("/")
    if len(parts) >= 3:
        return parts[0], parts[1], "/".join(parts[2:])
    elif len(parts) == 2:
        return parts[0], parts[1], ""
    return parts[0], "", ""


def load_yaml_types(yaml_dir):
    """Build mapping from asset path -> type by reading YAML files."""
    if yaml is None:
        return {}
    types = {}
    if not os.path.isdir(yaml_dir):
        return types
    for dirpath, _, filenames in os.walk(yaml_dir):
        for fn in filenames:
            if not fn.endswith(".yml"):
                continue
            yml_path = os.path.join(dirpath, fn)
            rel = os.path.relpath(yml_path, yaml_dir)
            parts = rel.split("/", 1)
            if len(parts) < 2:
                continue
            file_prefix = parts[1].rsplit(".yml", 1)[0]
            try:
                with open(yml_path) as f:
                    data = yaml.safe_load(f)
            except Exception:
                continue
            if not isinstance(data, dict):
                continue
            for key, val in data.items():
                if key == ":config:" or not isinstance(val, dict):
                    continue
                asset_type = val.get("type")
                if asset_type:
                    types[f"{file_prefix}/{key}"] = asset_type
    return types


def list_assets(manifest, category=None, file_name=None, asset_type=None):
    """Filter manifest keys by category/file/type."""
    assets = sorted(manifest.keys())

    if category:
        assets = [p for p in assets if parse_asset_path(p)[0] == category]
    if file_name:
        assets = [p for p in assets if parse_asset_path(p)[1] == file_name]
    if asset_type:
        yaml_types = load_yaml_types(MAIN_DIR)
        assets = [p for p in assets if yaml_types.get(p) == asset_type]

    return assets


def collect_yaml_files(assets, rom_version):
    """Determine which YAML files are needed for the given assets."""
    yaml_files = set()
    scene_dirs = set()

    for asset in assets:
        dirpart = "/".join(asset.split("/")[:-1])
        yaml_files.add(dirpart + ".yml")
        scene_dirs.add(dirpart)

    # Include room YAMLs for scene directories
    version_dir = os.path.join(MAIN_DIR, rom_version)
    for scene_dir in list(scene_dirs):
        if not scene_dir.startswith("scenes/"):
            continue
        parent = os.path.dirname(scene_dir)
        parent_path = os.path.join(version_dir, parent)
        if not os.path.isdir(parent_path):
            continue
        for fn in os.listdir(parent_path):
            if "_room_" not in fn or not fn.endswith(".yml"):
                continue
            room_path = os.path.join(parent_path, fn)
            try:
                with open(room_path) as f:
                    content = f.read(4096)
                if f"directory: {scene_dir}" in content:
                    yaml_files.add(os.path.join(parent, fn))
            except OSError:
                continue

    return yaml_files


def copy_yaml_with_externals(src, rel, scratch_dir, copied=None):
    """Copy a YAML file and its external_files dependencies to scratch dir.

    rel is relative to MAIN_DIR (e.g. 'objects/gameplay_keep.yml').
    External paths include the version prefix (e.g. 'pal_gc/misc/link_animetion.yml')
    and are relative to the srcdir root (where config.yml lives), so they copy
    directly to scratch_dir/<ext_path>.
    """
    if copied is None:
        copied = set()
    if rel in copied:
        return
    copied.add(rel)

    dest = os.path.join(scratch_dir, rel)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.copy2(src, dest)

    # Parse external_files from YAML without full YAML parsing (fast regex)
    try:
        with open(src) as f:
            content = f.read()
    except OSError:
        return

    in_externals = False
    for line in content.splitlines():
        if line.strip() == "external_files:":
            in_externals = True
            continue
        if in_externals:
            m = re.match(r"^\s+- (.+)$", line)
            if m:
                ext_path = m.group(1).strip()
                ext_src = os.path.join(MAIN_DIR, ext_path)
                if os.path.isfile(ext_src) and ext_path not in copied:
                    copy_yaml_with_externals(ext_src, ext_path, scratch_dir, copied)
            else:
                in_externals = False


def setup_scratch(yaml_files, rom_version):
    """Create scratch dir with config.yml and needed YAMLs."""
    scratch = tempfile.mkdtemp(prefix="torch_test_")
    version_dir = os.path.join(MAIN_DIR, rom_version)
    os.makedirs(os.path.join(scratch, rom_version), exist_ok=True)

    # Copy config.yml
    shutil.copy2(os.path.join(MAIN_DIR, "config.yml"), os.path.join(scratch, "config.yml"))

    # Copy YAMLs with externals
    # yaml_rel is like "objects/gameplay_keep.yml" (relative to version dir),
    # but copy_yaml_with_externals needs paths relative to scratch root
    # since external_files include the version prefix.
    copied = set()
    for yaml_rel in yaml_files:
        src = os.path.join(version_dir, yaml_rel)
        full_rel = os.path.join(rom_version, yaml_rel)
        if os.path.isfile(src):
            copy_yaml_with_externals(src, full_rel, scratch, copied)

    return scratch


def run_torch(scratch_dir, rom, work_dir):
    """Run torch and return path to generated O2R."""
    o2r_out = os.path.join(work_dir, "out")
    os.makedirs(o2r_out, exist_ok=True)

    log_path = os.path.join(work_dir, "torch.log")
    with open(log_path, "w") as log_f:
        result = subprocess.run(
            [TORCH, "o2r", "-s", scratch_dir, "-d", o2r_out, rom],
            stdout=log_f, stderr=subprocess.STDOUT
        )

    # Print critical/error lines
    with open(log_path) as f:
        for line in f:
            if re.search(r"\[(critical|error)\]", line):
                print(line.rstrip())

    o2r_file = os.path.join(o2r_out, "oot.o2r")
    if not os.path.isfile(o2r_file):
        print("ERROR: torch did not produce oot.o2r", file=sys.stderr)
        sys.exit(1)

    return o2r_file


def hash_assets_from_o2r(o2r_path, asset_set):
    """Extract and hash only the requested assets from the O2R."""
    hashes = {}
    with zipfile.ZipFile(o2r_path, "r") as zf:
        names = set(zf.namelist())
        for asset in asset_set:
            if asset not in names:
                continue
            data = zf.read(asset)
            hashes[asset] = hashlib.sha256(data).hexdigest()
    return hashes


def compare(assets, ref_manifest, gen_hashes, failures_only):
    """Compare generated hashes against reference manifest."""
    passed = 0
    failed = 0
    missing_gen = 0
    missing_ref = 0
    fail_list = []
    lines = []

    for asset in assets:
        gen_hash = gen_hashes.get(asset)
        ref_hash = ref_manifest.get(asset)

        if gen_hash is None:
            missing_gen += 1
            lines.append(f"MISSING {asset} (not in generated O2R)")
            fail_list.append(asset)
        elif ref_hash is None:
            missing_ref += 1
            lines.append(f"MISSING {asset} (not in reference manifest)")
            fail_list.append(asset)
        elif gen_hash == ref_hash:
            passed += 1
            if not failures_only:
                lines.append(f"PASS {asset}")
        else:
            failed += 1
            lines.append(f"FAIL {asset}")
            lines.append(f"  expected: {ref_hash}")
            lines.append(f"  got:      {gen_hash}")
            fail_list.append(asset)

    return passed, failed, missing_gen, missing_ref, fail_list, lines


def main():
    parser = argparse.ArgumentParser(description="Test OoT assets against reference O2R")
    parser.add_argument("rom", help="Path to ROM file")
    parser.add_argument("--category", help="Filter by category (e.g. objects, scenes)")
    parser.add_argument("--file", help="Filter by file name (e.g. gameplay_keep)")
    parser.add_argument("--type", help="Filter by asset type from YAML (e.g. GFX, VTX)")
    parser.add_argument("--from-file", help="Read asset paths from file")
    parser.add_argument("--o2r-out", help="Copy generated O2R to this directory")
    parser.add_argument("--failures-only", action="store_true", help="Only show failures")
    parser.add_argument("--rom-version", default="pal_gc", help="ROM version (default: pal_gc)")
    args = parser.parse_args()

    # Validate
    manifest_path = os.path.join(SOH_DIR, "manifests", f"{args.rom_version}.json")
    if not os.path.isfile(args.rom):
        print(f"ERROR: ROM not found: {args.rom}", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(manifest_path):
        print(f"ERROR: manifest not found: {manifest_path}", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(TORCH):
        print(f"ERROR: torch not found: {TORCH}", file=sys.stderr)
        sys.exit(1)

    # Setup logging
    os.makedirs(LOG_DIR, exist_ok=True)
    log_file = os.path.join(LOG_DIR, f"test_{time.strftime('%Y%m%d_%H%M%S')}.log")

    class Tee:
        def __init__(self, path):
            self.file = open(path, "w")
            self.stdout = sys.stdout
        def write(self, data):
            self.stdout.write(data)
            self.file.write(data)
        def flush(self):
            self.stdout.flush()
            self.file.flush()

    sys.stdout = Tee(log_file)
    print(f"Log: {log_file}")
    print()

    t0 = time.time()

    # Load reference manifest
    with open(manifest_path) as f:
        ref_manifest = json.load(f)

    # Build asset list
    if args.from_file:
        with open(args.from_file) as f:
            assets = [line.strip() for line in f if line.strip()]
    else:
        assets = list_assets(ref_manifest, args.category, args.file, args.type)

    if not assets:
        print("No assets matched the given filters.")
        sys.exit(0)

    print(f"Testing {len(assets)} assets...")
    print()

    # Collect YAMLs
    print(f"Collecting YAMLs... {elapsed(t0)}")
    yaml_files = collect_yaml_files(assets, args.rom_version)

    # Setup scratch dir
    print(f"Copying {len(yaml_files)} YAMLs to scratch dir... {elapsed(t0)}")
    scratch_dir = setup_scratch(yaml_files, args.rom_version)

    try:
        work_dir = tempfile.mkdtemp(prefix="torch_work_")
        try:
            # Run torch
            print(f"Running torch... {elapsed(t0)}")
            o2r_file = run_torch(scratch_dir, args.rom, work_dir)

            if args.o2r_out:
                os.makedirs(args.o2r_out, exist_ok=True)
                shutil.copy2(o2r_file, args.o2r_out)

            # Hash assets directly from zip (no extraction to disk)
            print(f"Hashing... {elapsed(t0)}")
            asset_set = set(assets)
            gen_hashes = hash_assets_from_o2r(o2r_file, asset_set)

            # Compare
            print(f"Comparing... {elapsed(t0)}")
            passed, failed, missing_gen, missing_ref, fail_list, lines = \
                compare(assets, ref_manifest, gen_hashes, args.failures_only)

            print(f"Done. {elapsed(t0)}")
            print()

            for line in lines:
                print(line)

            print()
            print("=== Summary ===")
            print(f"{passed} passed, {failed} failed, {missing_gen} not generated, {missing_ref} not in reference")
            print(f"Total: {passed + failed + missing_gen + missing_ref} assets")

            if fail_list:
                print()
                print("Failed assets:")
                for f_asset in fail_list:
                    print(f"  {f_asset}")

            sys.exit(1 if (failed + missing_gen + missing_ref) > 0 else 0)

        finally:
            shutil.rmtree(work_dir, ignore_errors=True)
    finally:
        shutil.rmtree(scratch_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
