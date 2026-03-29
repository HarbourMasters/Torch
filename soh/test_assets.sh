#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
resolve_paths

# Auto-log: tee all output to a timestamped log file
LOG_DIR="$SOH_DIR/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/test_$(date +%Y%m%d_%H%M%S).log"
exec > >(tee "$LOG_FILE") 2>&1
echo "Log: $LOG_FILE"
echo ""

usage() {
    cat <<'EOF'
Usage: test_assets.sh <rom> [options]

Options:
  --category <name>    Filter by category (e.g. objects, scenes, textures)
  --file <name>        Filter by file name (e.g. gameplay_keep)
  --type <name>        Filter by asset type from YAML (e.g. GFX, VTX, TEXTURE)
  --from <file>        Read asset paths from file (one per line)
  --scratch <dir>      Use custom scratch directory instead of default
  --o2r-out <dir>      Copy generated O2R to this directory for inspection
  --failures-only      Only show FAIL/MISSING lines
  -h, --help           Show this help

Examples:
  test_assets.sh soh/roms/pal_gc.z64 --category objects --type GFX
  test_assets.sh soh/roms/pal_gc.z64 --file gameplay_keep
  test_assets.sh soh/roms/pal_gc.z64 --from /tmp/failing_assets.txt
  test_assets.sh soh/roms/pal_gc.z64 --category objects --type GFX --failures-only
  test_assets.sh soh/roms/pal_gc.z64 --file object_horse --o2r-out /tmp/debug
EOF
    exit 1
}

if [[ $# -lt 1 ]]; then
    usage
fi

ROM="$1"
shift

CATEGORY=""
FILE=""
TYPE=""
FROM_FILE=""
O2R_OUT_DIR=""
FAILURES_ONLY=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --category) CATEGORY="$2"; shift 2 ;;
        --file) FILE="$2"; shift 2 ;;
        --type) TYPE="$2"; shift 2 ;;
        --from) FROM_FILE="$2"; shift 2 ;;
        --scratch) SCRATCH_DIR="$2"; shift 2 ;;
        --o2r-out) O2R_OUT_DIR="$2"; shift 2 ;;
        --failures-only) FAILURES_ONLY=true; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; usage ;;
    esac
done

validate_env "$ROM"

LIST_ASSETS="$SOH_DIR/tools/list_assets.py"

# Build asset list
if [[ -n "$FROM_FILE" ]]; then
    if [[ ! -f "$FROM_FILE" ]]; then
        echo "ERROR: File not found: $FROM_FILE" >&2
        exit 1
    fi
    mapfile -t ASSETS < "$FROM_FILE"
else
    LIST_CMD=(python3 "$LIST_ASSETS" --manifest "$MANIFEST")
    [[ -n "$CATEGORY" ]] && LIST_CMD+=(--category "$CATEGORY")
    [[ -n "$FILE" ]] && LIST_CMD+=(--file "$FILE")
    [[ -n "$TYPE" ]] && LIST_CMD+=(--type "$TYPE")

    mapfile -t ASSETS < <("${LIST_CMD[@]}")
fi

if [[ ${#ASSETS[@]} -eq 0 ]]; then
    echo "No assets matched the given filters."
    exit 0
fi

echo "Testing ${#ASSETS[@]} assets..."
echo ""

# Collect unique YAML files needed for the requested assets
declare -A YAML_FILES
declare -A SCENE_DIRS
for asset in "${ASSETS[@]}"; do
    yaml_rel="$(dirname "$asset").yml"
    YAML_FILES[$yaml_rel]=1
    # Track scene directories so we can find room YAMLs
    SCENE_DIRS["$(dirname "$asset")"]=1
done

# Also include room YAMLs that output into any of the scene directories.
# Room YAMLs live alongside scene YAMLs but use "directory:" to output
# into the scene's subdirectory (e.g. scenes/shared/spot01_room_0.yml
# outputs to scenes/shared/spot01_scene/).
for scene_dir in "${!SCENE_DIRS[@]}"; do
    parent="$(dirname "$scene_dir")"
    for room_yml in "$MAIN_DIR/$ROM_VERSION/$parent"/*_room_*.yml; do
        [[ -f "$room_yml" ]] || continue
        room_rel="${room_yml#$MAIN_DIR/$ROM_VERSION/}"
        # Check if this room's directory config points to our scene
        if grep -q "directory: $scene_dir" "$room_yml" 2>/dev/null; then
            YAML_FILES[$room_rel]=1
        fi
    done
done

# Copy all needed YAMLs into scratch dir (single setup)
setup_scratch_dir
for yaml_rel in "${!YAML_FILES[@]}"; do
    yaml_src="$MAIN_DIR/$ROM_VERSION/$yaml_rel"
    if [[ -f "$yaml_src" ]]; then
        copy_yaml_with_externals "$yaml_src" "$yaml_rel"
    fi
done

# Single Torch run
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

O2R_OUT="$WORK_DIR/out"
mkdir -p "$O2R_OUT"
"$TORCH" o2r -s "$SCRATCH_DIR" -d "$O2R_OUT" "$ROM" > "$WORK_DIR/torch.log" 2>&1
grep -E '^\[.*\] \[(critical|error)\]' "$WORK_DIR/torch.log" || true

O2R_FILE="$O2R_OUT/oot.o2r"
if [[ ! -f "$O2R_FILE" ]]; then
    echo "ERROR: torch did not produce oot.o2r"
    exit 1
fi

if [[ -n "$O2R_OUT_DIR" ]]; then
    mkdir -p "$O2R_OUT_DIR"
    cp "$O2R_FILE" "$O2R_OUT_DIR/"
fi

# Hash only the assets we care about (instead of extracting the entire O2R)
EXTRACT_DIR="$WORK_DIR/extracted"
mkdir -p "$EXTRACT_DIR"

# Build the list of files to extract
ASSET_LIST="$WORK_DIR/asset_list.txt"
printf '%s\n' "${ASSETS[@]}" > "$ASSET_LIST"

# Extract only the matching assets from the generated O2R
unzip -qo "$O2R_FILE" $(cat "$ASSET_LIST") -d "$EXTRACT_DIR" 2>/dev/null || true

# Hash all extracted files in a single sha256sum call, convert to JSON manifest
GEN_MANIFEST="$WORK_DIR/gen_manifest.json"
(cd "$EXTRACT_DIR" && find . -type f -print0 | xargs -0 sha256sum) | \
    sed 's|^\([a-f0-9]*\)  \./|\1\t|' | \
    awk -F'\t' 'BEGIN{printf "{"} NR==1{printf "\n  \"%s\": \"%s\"", $2, $1} NR>1{printf ",\n  \"%s\": \"%s\"", $2, $1} END{print "\n}"}' \
    > "$GEN_MANIFEST"

# Build asset filter file
ASSET_JSON="$WORK_DIR/assets.json"
printf '%s\n' "${ASSETS[@]}" | jq -Rs '[split("\n")[] | select(length > 0)]' > "$ASSET_JSON"

# Compare manifests and output results + fail count on last line
RESULT=$(jq -rn \
    --slurpfile ref "$MANIFEST" \
    --slurpfile gen "$GEN_MANIFEST" \
    --slurpfile assets "$ASSET_JSON" \
    --arg failures_only "$FAILURES_ONLY" \
'
{
    pass: 0, fail: 0, missing_gen: 0, missing_ref: 0,
    lines: [], fail_list: []
} as $init |
$ref[0] as $r | $gen[0] as $g |
reduce $assets[0][] as $asset ($init;
    if ($g[$asset] == null) then
        .missing_gen += 1
        | .lines += ["MISSING \($asset) (not in generated O2R)"]
        | .fail_list += [$asset]
    elif ($r[$asset] == null) then
        .missing_ref += 1
        | .lines += ["MISSING \($asset) (not in reference manifest)"]
        | .fail_list += [$asset]
    elif ($g[$asset] == $r[$asset]) then
        .pass += 1
        | if $failures_only == "false" then .lines += ["PASS \($asset)"] else . end
    else
        .fail += 1
        | .lines += ["FAIL \($asset)", "  expected: \($r[$asset])", "  got:      \($g[$asset])"]
        | .fail_list += [$asset]
    end
)
| .lines[], "",
  "=== Summary ===",
  "\(.pass) passed, \(.fail) failed, \(.missing_gen) not generated, \(.missing_ref) not in reference",
  "Total: \(.pass + .fail + .missing_gen + .missing_ref) assets",
  (if (.fail_list | length) > 0 then
    "", "Failed assets:", (.fail_list[] | "  \(.)")
  else empty end),
  "FAIL_COUNT=\(.fail + .missing_gen + .missing_ref)"
')

# Print everything except the FAIL_COUNT line
echo "$RESULT" | grep -v '^FAIL_COUNT='

# Extract fail count and exit
FAIL_COUNT=$(echo "$RESULT" | grep '^FAIL_COUNT=' | cut -d= -f2)
[[ "$FAIL_COUNT" -gt 0 ]] && exit 1
exit 0
