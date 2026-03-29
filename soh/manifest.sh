#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
O2R_DIR="$SCRIPT_DIR/o2r"

O2R="${1:-$O2R_DIR/reference.o2r}"
MANIFEST_OUT="${2:-$O2R_DIR/manifest.json}"

if [[ ! -f "$O2R" ]]; then
    echo "ERROR: O2R not found at $O2R" >&2
    exit 1
fi

WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

echo "Extracting $O2R..." >&2
unzip -qo "$O2R" -d "$WORK_DIR"

echo "Hashing files..." >&2
(cd "$WORK_DIR" && find . -type f -print0 | sort -z | xargs -0 sha256sum) | \
    sed 's|^\([a-f0-9]*\)  \./|\1\t|' | \
    awk -F'\t' 'NR==1{printf "{\n  \"%s\": \"%s\"", $2, $1} NR>1{printf ",\n  \"%s\": \"%s\"", $2, $1} END{print "\n}"}' \
    > "$MANIFEST_OUT"

COUNT=$(jq length "$MANIFEST_OUT")
echo "Wrote manifest with $COUNT entries to $MANIFEST_OUT" >&2
