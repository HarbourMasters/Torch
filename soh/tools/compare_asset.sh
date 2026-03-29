#!/usr/bin/env bash
# Compare a single asset between the reference O2R and a freshly generated one.
# Usage: compare_asset.sh <rom> <asset-path>
#
# Extracts the asset from both the reference O2R and a Torch-generated O2R,
# then shows hex dumps side by side for debugging.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOH_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <rom> <asset-path>"
    echo "Example: $0 soh/roms/pal_gc_0227d7.z64 objects/object_link_child/gLinkChildDekuShieldMtx"
    exit 1
fi

ROM="$1"
ASSET="$2"
REFERENCE="${3:-$SOH_DIR/o2r/reference.o2r}"
TORCH="$SOH_DIR/../build/torch"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Extract from reference
if ! unzip -qo "$REFERENCE" "$ASSET" -d "$WORK/ref" 2>/dev/null; then
    echo "ERROR: Asset '$ASSET' not found in reference O2R"
    exit 1
fi

# Generate O2R and extract
echo "Generating O2R..."
"$TORCH" o2r -s "$SOH_DIR/assets/yml" -d "$WORK/out" "$ROM" > "$WORK/torch.log" 2>&1

O2R_FILE="$WORK/out/oot.o2r"
if [[ ! -f "$O2R_FILE" ]]; then
    echo "ERROR: torch did not produce oot.o2r"
    exit 1
fi

if ! unzip -qo "$O2R_FILE" "$ASSET" -d "$WORK/gen" 2>/dev/null; then
    echo "ERROR: Asset '$ASSET' not found in generated O2R"
    echo ""
    echo "=== REF ($(wc -c < "$WORK/ref/$ASSET") bytes) ==="
    xxd "$WORK/ref/$ASSET"
    exit 1
fi

REF_FILE="$WORK/ref/$ASSET"
GEN_FILE="$WORK/gen/$ASSET"
REF_SIZE=$(wc -c < "$REF_FILE")
GEN_SIZE=$(wc -c < "$GEN_FILE")

REF_HASH=$(sha256sum "$REF_FILE" | awk '{print $1}')
GEN_HASH=$(sha256sum "$GEN_FILE" | awk '{print $1}')

if [[ "$REF_HASH" = "$GEN_HASH" ]]; then
    echo "PASS $ASSET ($REF_SIZE bytes)"
    exit 0
fi

echo "FAIL $ASSET"
echo "  ref: $REF_SIZE bytes  sha256: $REF_HASH"
echo "  gen: $GEN_SIZE bytes  sha256: $GEN_HASH"
echo ""
echo "=== REF ==="
xxd "$REF_FILE"
echo ""
echo "=== GEN ==="
xxd "$GEN_FILE"
echo ""
echo "=== DIFF ==="
diff <(xxd "$REF_FILE") <(xxd "$GEN_FILE") || true
