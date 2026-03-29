#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
resolve_paths

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <rom> <asset-path> [<asset-path> ...]"
    echo "Example: $0 soh/roms/pal_gc.z64 text/elf_message_field/elf_message_field"
    echo ""
    echo "Asset path format: <category>/<file>/<asset_name>"
    echo "The YAML is looked up as \$ROM_VERSION/<category>/<file>.yml (default: pal_gc)"
    exit 1
fi

ROM="$1"
shift

validate_env "$ROM"
setup_scratch_dir

# Copy needed YAMLs into scratch dir
for ASSET in "$@"; do
    YAML_REL="$(dirname "$ASSET").yml"
    YAML_SRC="$MAIN_DIR/$ROM_VERSION/$YAML_REL"

    if [[ ! -f "$YAML_SRC" ]]; then
        echo "WARNING: YAML not found: $YAML_SRC"
        continue
    fi

    copy_yaml_with_externals "$YAML_SRC" "$YAML_REL"
done

WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

# Generate O2R using scratch dir
O2R_OUT="$WORK_DIR/out"
mkdir -p "$O2R_OUT"
"$TORCH" o2r -s "$SCRATCH_DIR" -d "$O2R_OUT" "$ROM" 2>&1 | grep -E '^\[.*\] \[(critical|error)\]' || true

O2R_FILE="$O2R_OUT/oot.o2r"
if [[ ! -f "$O2R_FILE" ]]; then
    echo "ERROR: torch did not produce oot.o2r"
    exit 1
fi

# Extract and compare each asset
EXTRACT_DIR="$WORK_DIR/extracted"
unzip -q "$O2R_FILE" -d "$EXTRACT_DIR"

PASS=0
FAIL=0
MISSING_REF=0
MISSING_GEN=0

for ASSET in "$@"; do
    result=$(compare_asset "$ASSET" "$EXTRACT_DIR") || true
    echo "$result"

    case "$result" in
        PASS*) ((PASS++)) || true ;;
        FAIL*) ((FAIL++)) || true ;;
        *"not in generated"*) ((MISSING_GEN++)) || true ;;
        *"not in reference"*) ((MISSING_REF++)) || true ;;
    esac
done

echo "---"
echo "$PASS passed, $FAIL failed, $MISSING_GEN not generated, $MISSING_REF not in reference"
