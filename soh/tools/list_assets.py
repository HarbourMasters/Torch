#!/usr/bin/env python3
"""List asset paths from manifest.json, with optional filtering by category, file, or type.

Usage:
    python3 soh/tools/list_assets.py --manifest soh/o2r/manifest.json --summary
    python3 soh/tools/list_assets.py --manifest soh/o2r/manifest.json --category objects
    python3 soh/tools/list_assets.py --manifest soh/o2r/manifest.json --category objects --type GFX
    python3 soh/tools/list_assets.py --manifest soh/o2r/manifest.json --file gameplay_keep
"""

import argparse
import json
import os
import signal
import sys
from collections import defaultdict

signal.signal(signal.SIGPIPE, signal.SIG_DFL)

import yaml


def parse_asset_path(path):
    """Split an asset path into (category, file, asset_name).

    Asset paths in manifest.json look like: objects/gameplay_keep/gSomeDL
    """
    parts = path.split("/")
    if len(parts) >= 3:
        return parts[0], parts[1], "/".join(parts[2:])
    elif len(parts) == 2:
        return parts[0], parts[1], ""
    return parts[0], "", ""


def load_yaml_types(yaml_dir):
    """Build a mapping from asset path -> type by reading YAML files.

    Returns dict like {"objects/gameplay_keep/gSomeDL": "GFX", ...}
    """
    types = {}
    if not os.path.isdir(yaml_dir):
        return types

    for dirpath, _, filenames in os.walk(yaml_dir):
        for fn in filenames:
            if not fn.endswith(".yml"):
                continue
            yml_path = os.path.join(dirpath, fn)
            rel = os.path.relpath(yml_path, yaml_dir)
            # rel is like "pal_gc/objects/gameplay_keep.yml"
            # strip version prefix and .yml suffix to get "objects/gameplay_keep"
            parts = rel.split("/", 1)
            if len(parts) < 2:
                continue
            file_prefix = parts[1].rsplit(".yml", 1)[0]  # "objects/gameplay_keep"

            try:
                with open(yml_path) as f:
                    data = yaml.safe_load(f)
            except (yaml.YAMLError, OSError):
                continue

            if not isinstance(data, dict):
                continue

            for key, val in data.items():
                if key == ":config:" or not isinstance(val, dict):
                    continue
                asset_type = val.get("type")
                if asset_type:
                    asset_path = f"{file_prefix}/{key}"
                    types[asset_path] = asset_type

    return types


def main():
    parser = argparse.ArgumentParser(description="List asset paths from manifest.json")
    parser.add_argument("--manifest", required=True, help="Path to manifest.json")
    parser.add_argument("--yaml-dir", default=None,
                        help="Path to YAML directory (default: auto-detect from manifest location)")
    parser.add_argument("--category", help="Filter by category (e.g. objects, scenes, textures)")
    parser.add_argument("--file", help="Filter by file name (e.g. gameplay_keep)")
    parser.add_argument("--type", help="Filter by asset type from YAML (e.g. GFX, VTX, TEXTURE)")
    parser.add_argument("--summary", action="store_true", help="Show category and type counts")
    args = parser.parse_args()

    with open(args.manifest) as f:
        manifest = json.load(f)

    assets = sorted(manifest.keys())

    # Auto-detect yaml_dir: manifest is at soh/o2r/manifest.json, YAMLs at soh/assets/yml/
    if args.yaml_dir is None:
        manifest_dir = os.path.dirname(os.path.abspath(args.manifest))
        args.yaml_dir = os.path.join(os.path.dirname(manifest_dir), "assets", "yml")

    # Load type info if needed
    yaml_types = {}
    if args.type or args.summary:
        yaml_types = load_yaml_types(args.yaml_dir)

    if args.summary:
        cat_counts = defaultdict(int)
        type_counts = defaultdict(int)
        for path in assets:
            cat, _, _ = parse_asset_path(path)
            cat_counts[cat] += 1
            if path in yaml_types:
                type_counts[yaml_types[path]] += 1

        print("Categories:")
        for cat, count in sorted(cat_counts.items(), key=lambda x: -x[1]):
            print(f"  {cat}: {count}")

        if yaml_types:
            print(f"\nTypes (from {len(yaml_types)} YAML-defined assets):")
            for t, count in sorted(type_counts.items(), key=lambda x: -x[1]):
                print(f"  {t}: {count}")
            untyped = len(assets) - sum(1 for a in assets if a in yaml_types)
            if untyped:
                print(f"  (no YAML): {untyped}")

        print(f"\nTotal: {len(assets)}")
        return

    # Filter
    filtered = assets
    if args.category:
        filtered = [p for p in filtered if parse_asset_path(p)[0] == args.category]
    if args.file:
        filtered = [p for p in filtered if parse_asset_path(p)[1] == args.file]
    if args.type:
        filtered = [p for p in filtered if yaml_types.get(p) == args.type]

    for path in filtered:
        print(path)


if __name__ == "__main__":
    main()
