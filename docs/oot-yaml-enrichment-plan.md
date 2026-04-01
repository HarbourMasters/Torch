# Enrich YAML to Eliminate Auto-Discovery Overhead

## Context

Torch takes ~35s for OoT O2R generation (35,386 assets, 1,065 files). ~60% of runtime is kernel page faults from memory allocation. The top page fault caller is `Companion::AddAsset`, which auto-discovers ~1,000-1,400 assets during DList/Scene/Skeleton parsing. Each discovery triggers YAML::Node creation, factory->parse() (ROM decompression + binary read), and RegisterAsset (hashmap insertion).

AddAsset already short-circuits if the asset exists (line 1928-1936): it calls GetNodeByAddr, finds the pre-declared entry, returns early. **No Torch changes needed** — just pre-declare the assets in YAML.

This is also the foundation for a future codegen approach.

## Existing pattern

There's already a working pipeline for VTX enrichment:
1. `soh/tools/extract_vtx.py` — reads reference O2R, extracts VTX metadata → JSON
2. `soh/tools/zapd_to_torch.py --vtx-json` — reads JSON, injects VTX entries into YAML files

We generalize this to extract ALL auto-discovered asset types from the reference O2R.

## What gets auto-discovered today

| Type | Source Factory | GBI/Trigger | Est. Count |
|------|---------------|-------------|-----------|
| GFX (child DLists) | DisplayListFactory:868,957 | G_DL, G_BRANCH_Z | ~500-600 |
| VTX (consolidated) | DeferredVtx:110 | G_VTX merged | ~200-300 |
| MTX | DisplayListFactory:1017 | G_MTX | ~50-100 |
| LIGHTS | DisplayListFactory:921 | G_MOVEMEM | ~50-100 |
| GFX (scene meshes) | OoTSceneFactory:168 | SetMesh commands | ~100+ |
| OOT:COLLISION | OoTSceneFactory:493 | Scene collision refs | ~20-50 |
| OOT:LIMB | OoTSkeletonFactory:389 | Skeleton processing | ~50-100 |
| BLOB (limb tables) | OoTSkeletonFactory:415 | Limb array pointers | ~10-20 |

## Implementation

### Step 1: Generalize extract_vtx.py → extract_assets.py

Create `soh/tools/extract_assets.py` that reads a reference O2R and extracts metadata for ALL asset types (not just VTX). The reference O2R contains every asset Torch produces — both pre-declared and auto-discovered.

**How to identify auto-discovered assets**: Compare what's in the O2R against what's in the current YAML. Anything in the O2R that doesn't have a corresponding YAML entry is auto-discovered.

Alternatively, simpler: extract every asset from the O2R that matches the auto-discovered types (GFX, MTX, LIGHTS, VTX/OOT:ARRAY) and only inject those that don't already exist in YAML (the `add_vtx_to_yaml` function already does this dedup check).

For each asset in the O2R, extract:
- Asset path (encodes source file + asset name)
- Type (from the binary resource header)
- Offset (from the asset name pattern, e.g. `Vtx_001980` → `0x06001980`)
- Count (from binary data where applicable)
- Symbol (from asset name)

Output: JSON grouped by source YAML file, same format as vtx.json.

### Step 2: Generalize add_vtx_to_yaml → add_assets_to_yaml

Extend `zapd_to_torch.py` (or `extract_assets.py` itself) to inject any asset type into YAML, not just VTX. The injection format varies by type:

```yaml
# GFX
gameplay_keepDL_005C98:
  type: GFX
  offset: 0x06005C98
  symbol: gameplay_keepDL_005C98

# MTX
gameplay_keepMtx_000000:
  type: MTX
  offset: 0x06008A40
  symbol: gameplay_keepMtx_000000

# LIGHTS
gameplay_keepLights_001234:
  type: LIGHTS
  offset: 0x06001234

# OOT:ARRAY (VTX) — already handled
gameplay_keepVtx_001980:
  type: OOT:ARRAY
  offset: 0x06001980
  count: 32
  array_type: VTX
  symbol: gameplay_keepVtx_001980
```

### Step 3: Update build pipeline

The port's YAML generation pipeline becomes:
1. `zapd_to_torch.py` — convert ZAPD XML → base YAML (existing)
2. `extract_assets.py reference.o2r` → assets.json (new)
3. `zapd_to_torch.py --assets-json assets.json` — enrich YAML with auto-discovered assets (new)

The reference O2R is generated once from OTRExporter (or from a baseline Torch run) and checked into the repo alongside the VTX JSON.

### Step 4: Measure

Run Torch on enriched YAML, compare timing. AddAsset calls should now early-return for all pre-declared assets. Verify 35,386/35,386 and binary match.

## Files to create/modify

- `soh/tools/extract_assets.py` — **new**, generalized asset extractor from reference O2R
- `soh/tools/zapd_to_torch.py` — extend to accept `--assets-json` and inject all types
- No Torch source changes

## Expected impact

- ~1,000-1,400 fewer factory->parse() calls during O2R generation
- Each avoided parse saves: YAML::Node creation + ROM decompression + binary read + RegisterAsset
- Conservative estimate: 1-7s savings depending on how much allocation overhead is in the auto-discovery path vs base asset parsing
- This is a stepping stone: enriched YAML is the prerequisite for future codegen

## Risks

- Reference O2R is ROM-version-specific. Different ROM versions may produce different auto-discovered assets. Mitigation: generate one reference O2R per supported ROM version (already how VTX JSON works).
- Symbol naming must match what Torch's AddAsset would generate. If there's a mismatch, AddAsset won't find the pre-declared entry and will try to create a duplicate, hitting the type clash error at line 1932.
- Some auto-discovered assets have fields that are hard to extract from the O2R binary (e.g. VTX count). Need to parse the resource header format.

## Verification

1. Generate reference O2R from current (non-enriched) YAML
2. Run extract_assets.py on it → assets.json
3. Enrich YAML with assets.json
4. Run Torch on enriched YAML → verify 35,386/35,386 + binary match
5. Compare timing: enriched vs non-enriched
