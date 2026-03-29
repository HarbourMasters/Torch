#!/usr/bin/env bash
# Shared functions for OoT asset testing scripts.
# Source this file: source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

# Resolve standard paths relative to soh/ directory
# Sets: SOH_DIR, O2R_DIR, MANIFEST, TORCH, SCRATCH_DIR, MAIN_DIR
resolve_paths() {
    SOH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    O2R_DIR="$SOH_DIR/o2r"
    ROM_VERSION="${ROM_VERSION:-pal_gc}"
    MANIFEST="$SOH_DIR/manifests/${ROM_VERSION}.json"
    TORCH="$SOH_DIR/../build/torch"
    SCRATCH_DIR=$(mktemp -d)
    MAIN_DIR="$SOH_DIR/assets/yml"
}

# Validate that required files exist
# Arguments: $1 = ROM path
validate_env() {
    local rom="$1"
    if [[ ! -f "$rom" ]]; then
        echo "ERROR: ROM not found at $rom" >&2
        return 1
    fi
    if [[ ! -f "$MANIFEST" ]]; then
        echo "ERROR: manifest.json not found at $MANIFEST" >&2
        echo "Run manifest.sh first" >&2
        return 1
    fi
    if [[ ! -x "$TORCH" ]]; then
        echo "ERROR: torch binary not found at $TORCH" >&2
        return 1
    fi
}

# Clean and prepare the scratch directory
setup_scratch_dir() {
    find "$SCRATCH_DIR/$ROM_VERSION" -mindepth 1 -not -name '.gitkeep' -delete 2>/dev/null || true
    mkdir -p "$SCRATCH_DIR/$ROM_VERSION"
    # Ensure config.yml is present (needed when using a custom scratch dir)
    if [[ ! -f "$SCRATCH_DIR/config.yml" ]]; then
        cp "$MAIN_DIR/config.yml" "$SCRATCH_DIR/config.yml"
    fi
}

# Copy a YAML file and its external dependencies into the scratch directory
# Recursively follows external_files declarations.
# Arguments: $1 = source YAML path, $2 = relative path within pal_gc/
copy_yaml_with_externals() {
    local yaml_src="$1"
    local yaml_rel="$2"

    mkdir -p "$SCRATCH_DIR/$ROM_VERSION/$(dirname "$yaml_rel")"
    cp "$yaml_src" "$SCRATCH_DIR/$ROM_VERSION/$yaml_rel"

    # Copy external file dependencies (recursively)
    local ext_paths
    ext_paths=$(grep -A100 '^  external_files:' "$yaml_src" 2>/dev/null | grep '^ \+- ' | sed 's/^ *- *//' || true)
    if [[ -n "$ext_paths" ]]; then
        while read -r ext_path; do
            local ext_src="$MAIN_DIR/$ext_path"
            if [[ -f "$ext_src" && ! -f "$SCRATCH_DIR/$ext_path" ]]; then
                mkdir -p "$SCRATCH_DIR/$(dirname "$ext_path")"
                cp "$ext_src" "$SCRATCH_DIR/$ext_path"
                # Recurse into the external file's own dependencies
                copy_yaml_with_externals "$ext_src" "$ext_path"
            fi
        done <<< "$ext_paths"
    fi
}

# Compare a single asset against the reference manifest
# Arguments: $1 = asset path, $2 = extract directory
# Outputs: "PASS <path>", "FAIL <path>", or "MISSING <path>"
# Returns: 0 for pass, 1 for fail/missing
compare_asset() {
    local asset="$1"
    local extract_dir="$2"
    local generated="$extract_dir/$asset"

    if [[ ! -f "$generated" ]]; then
        echo "MISSING $asset (not in generated O2R)"
        return 1
    fi

    local ref_hash
    ref_hash=$(jq -r --arg k "$asset" '.[$k] // empty' "$MANIFEST")
    if [[ -z "$ref_hash" ]]; then
        echo "MISSING $asset (not in reference manifest)"
        return 1
    fi

    local gen_hash
    gen_hash=$(sha256sum "$generated" | awk '{print $1}')

    if [[ "$gen_hash" = "$ref_hash" ]]; then
        echo "PASS $asset"
        return 0
    else
        echo "FAIL $asset"
        echo "  expected: $ref_hash"
        echo "  got:      $gen_hash"
        return 1
    fi
}
