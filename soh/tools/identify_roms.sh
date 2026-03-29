#!/usr/bin/env bash
# Identify and rename OoT ROM files by SHA1 hash.
# Usage: ./identify_roms.sh [rom_dir]
#
# Computes SHA1 for each file in rom_dir (default: soh/roms/),
# matches against known OoT ROM hashes, and renames matches to
# <dir_name>_<first 6 chars of sha>.z64
#
# Handles duplicates: if a matching renamed file already exists,
# the duplicate is deleted.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOH_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROM_DIR="${1:-$SOH_DIR/roms}"

# Known ROM SHA1 prefixes (first 8 chars) → "dir_name|description"
declare -A KNOWN_ROMS=(
    # PAL N64
    ["328a1f1b"]="pal_1-0|PAL 1.0"
    ["cfbb98d3"]="pal_1-1|PAL 1.1"
    # PAL GC
    ["0227d7c0"]="pal_gc|PAL GC"
    ["f4623943"]="pal_mq|PAL MQ"
    # NTSC-U N64
    ["ad69c911"]="ntsc_u_1-0|NTSC-U 1.0"
    ["d3ecb253"]="ntsc_u_1-1|NTSC-U 1.1"
    ["41b3bdc4"]="ntsc_u_1-2|NTSC-U 1.2"
    # NTSC-U GC
    ["b82710ba"]="ntsc_u_gc|NTSC-U GC"
    ["8b5d13aa"]="ntsc_u_mq|NTSC-U MQ"
    # NTSC-J N64
    ["c892bbda"]="ntsc_j_1-0|NTSC-J 1.0"
    ["dbfc81f6"]="ntsc_j_1-1|NTSC-J 1.1"
    ["fa5f5942"]="ntsc_j_1-2|NTSC-J 1.2"
    # NTSC-J GC
    ["0769c846"]="ntsc_j_gc|NTSC-J GC"
    ["dd14e143"]="ntsc_j_mq|NTSC-J MQ"
    ["2ce2d1a9"]="ntsc_j_gc_collection|NTSC-J GC (Collection)"
    # Debug
    ["cee6bc3c"]="pal_gc_dbg|PAL GC (Debug)"
    # PAL MQ Debug has 3 known dumps
    ["079b855b"]="pal_mq_dbg|PAL MQ (Debug) dump 1"
    ["50bebeda"]="pal_mq_dbg|PAL MQ (Debug) dump 2"
    ["cfecfdc5"]="pal_mq_dbg|PAL MQ (Debug) dump 3"
)

matched=0
duplicates=0
unknown=0

# Two-pass approach: hash everything first, then rename/dedupe.

declare -A file_hashes   # path → sha1
declare -A seen_targets   # target filename → source path (first one wins)

echo "=== Pass 1: Computing SHA1 hashes ==="
echo ""

mapfile -d '' files < <(find "$ROM_DIR" -maxdepth 1 -type f -print0 2>/dev/null)

for romfile in "${files[@]}"; do
    [ -n "$romfile" ] || continue
    bn="$(basename "$romfile")"
    case "$bn" in
        .gitignore|README.md|*.sh|*.txt|*.json) continue ;;
    esac

    echo "  Hashing: $bn"
    sha1="$(sha1sum "$romfile" | awk '{print $1}')"
    file_hashes["$romfile"]="$sha1"
done

echo ""
echo "=== Pass 2: Identifying, renaming, and deduplicating ==="
echo ""

for romfile in "${!file_hashes[@]}"; do
    sha1="${file_hashes[$romfile]}"
    bn="$(basename "$romfile")"
    prefix="${sha1:0:8}"

    if [[ -v "KNOWN_ROMS[$prefix]" ]]; then
        IFS='|' read -r dir_name description <<< "${KNOWN_ROMS[$prefix]}"
        short_sha="${sha1:0:6}"
        new_name="${dir_name}_${short_sha}.z64"

        # Check for duplicate
        if [[ -v "seen_targets[$new_name]" ]]; then
            # Already have this one — delete the dupe
            if [ "$bn" = "$new_name" ]; then
                # This IS the canonical file; the earlier one was renamed to it
                # This shouldn't happen, but skip just in case
                echo "  OK      $new_name — $description"
            else
                rm -- "$romfile"
                echo "  DUPE    $bn (same as $new_name) — deleted"
            fi
            echo "  SHA1:   $sha1"
            ((duplicates++)) || true
        elif [ "$bn" = "$new_name" ]; then
            # Already correctly named
            seen_targets["$new_name"]="$romfile"
            echo "  OK      $new_name — $description"
            echo "  SHA1:   $sha1"
            ((matched++)) || true
        elif [ -f "$ROM_DIR/$new_name" ]; then
            # Target name already exists on disk (from previous run)
            rm -- "$romfile"
            seen_targets["$new_name"]="$ROM_DIR/$new_name"
            echo "  DUPE    $bn (already have $new_name) — deleted"
            echo "  SHA1:   $sha1"
            ((duplicates++)) || true
        else
            mv -- "$romfile" "$ROM_DIR/$new_name"
            seen_targets["$new_name"]="$ROM_DIR/$new_name"
            echo "  RENAMED $bn"
            echo "       →  $new_name — $description"
            echo "  SHA1:   $sha1"
            ((matched++)) || true
        fi
    else
        echo "  UNKNOWN $bn"
        echo "  SHA1:   $sha1"
        ((unknown++)) || true
    fi
    echo ""
done

echo "--- Summary ---"
echo "Matched:    $matched"
echo "Duplicates: $duplicates (deleted)"
echo "Unknown:    $unknown"
echo "Expected:   17 versions (19 hashes including debug dumps)"

if [ "$unknown" -gt 0 ]; then
    echo ""
    echo "Unknown files were left in place. Delete them manually if they're not needed."
fi
