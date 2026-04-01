# Full YAML Enrichment: Remove Auto-Discovery from Torch

## Context

Torch currently auto-discovers ~6,400 assets at runtime via `AddAsset` during factory
parsing (DLists, MTX, LIGHTS, scene meshes, skeleton limbs, etc.). This is expensive
(~8s of the 35s runtime) and creates naming conflicts for scene assets that appear
under multiple alternate header paths.

The goal: YAML is the single source of truth for all assets. Torch expects fully
enriched YAML and errors on undeclared references instead of silently creating them.
This eliminates auto-discovery overhead, resolves the duplicate-name scene issue,
and is the foundation for future codegen.

## Current auto-discovery call sites (to remove)

| Factory | What it discovers | Trigger | Lines | Est. Count |
|---------|------------------|---------|-------|-----------|
| DisplayListFactory | Child DLists | G_DL, G_BRANCH_Z opcodes | 868, 957 | ~3,900 |
| DisplayListFactory | MTX | G_MTX opcode | 1017 | ~100 |
| DisplayListFactory | LIGHTS | G_MOVEMEM opcode | 921 | ~50 |
| DeferredVtx | Consolidated VTX | G_VTX merged arrays | 110 | ~200 (already handled by VTX JSON) |
| OoTSceneFactory | Room mesh DLists | SetMesh scene commands | 168 | ~1,800 |
| OoTSceneFactory | Collision | Scene collision refs | 493 | ~300 |
| OoTSkeletonFactory | Limb DLists | Skeleton limb pointers | 70 | ~2,000 |
| OoTSkeletonFactory | Limb entries | Skeleton processing | 389 | ~50 |
| OoTSkeletonFactory | Limb table BLOBs | Limb array pointers | 415 | ~10 |

## Approach

### Phase 1: Enrichment tooling (partially done)

Extend `catalog_undeclared.py` and `zapd_to_torch.py` to pre-declare ALL asset types
from the reference O2R. Currently handles GFX and MTX for objects. Needs:

- Scene DLists (with canonical naming — one name per offset, no `Set_` variants)
- LIGHTS
- OOT:COLLISION
- OOT:LIMB and BLOB (skeleton data)
- Any other types that AddAsset creates

The canonical name for each offset should match what ZAPD/OTRExporter uses. The
reference O2R already contains the correct names — use the first occurrence at each
offset as the canonical name.

### Phase 2: Make Torch expect enriched YAML

For each auto-discovery site, change from "create if missing" to "error if missing":

```cpp
// Before (auto-discovery):
const auto decl = this->GetNodeByAddr(w1);
if (!decl.has_value()) {
    YAML::Node gfx;
    gfx["type"] = "GFX";
    gfx["offset"] = w1;
    Companion::Instance->AddAsset(gfx);
}

// After (expect enriched):
const auto decl = this->GetNodeByAddr(w1);
if (!decl.has_value()) {
    SPDLOG_ERROR("Undeclared GFX at 0x{:X} — YAML enrichment incomplete", w1);
    // Continue gracefully (don't crash), but log the gap
}
```

This can be done incrementally per factory. Each factory migration:
1. Ensure the enrichment tooling covers all assets that factory discovers
2. Remove the AddAsset call
3. Add error logging for undeclared references
4. Test: 35,386/35,386 + binary match

### Phase 3: Remove AddAsset infrastructure

Once all factories are migrated:
- Remove `Companion::AddAsset()` entirely (or keep as error-only stub)
- Remove `autogen` field handling
- Remove `RegisterAsset()` (only used by AddAsset)
- Simplify `ProcessFile` — no more recursive AddAsset → RegisterAsset → ParseNode chains

### Phase 4: Scene alternate headers

The `Set_XXXXXX` naming issue: OoT scenes have alternate headers (child night,
adult day, etc.) that reference the same room DLists. Currently each header set
triggers auto-discovery with a different name prefix.

In enriched YAML: each DList is declared once with its canonical name. Scene
alternate header processing just looks up the existing declaration by offset —
no need for `Set_` variant names. The binary output uses the canonical path for
CRC64 hashing, which will match the reference O2R.

This may require changes to OoTSceneFactory to not generate alternate-header-specific
names when looking up DLists.

## Migration order (by impact)

1. **Scene mesh DLists** (OoTSceneFactory:168) — ~1,800 assets, biggest scene savings
2. **Skeleton limb DLists** (OoTSkeletonFactory:70) — ~2,000 assets
3. **Child/branch DLists** (DisplayListFactory:868,957) — ~3,900 assets (partially done for objects)
4. **MTX** (DisplayListFactory:1017) — ~100 assets (partially done for objects)
5. **LIGHTS** (DisplayListFactory:921) — ~50 assets
6. **Collision** (OoTSceneFactory:493) — ~300 assets
7. **Skeleton limbs/BLOBs** (OoTSkeletonFactory:389,415) — ~60 assets
8. **DeferredVtx** — already handled by VTX JSON enrichment

## Expected impact

- Eliminate ~8s of AddAsset overhead (35s → 27s, measured)
- Simplify Torch's processing pipeline (no recursive AddAsset chains)
- Fix scene alternate header naming conflicts
- Foundation for codegen (fully-declared YAML = complete asset manifest)

## Files to modify

### Enrichment tooling (no Torch changes)
- `soh/tools/catalog_undeclared.py` — extend for all asset types + scene canonical naming
- `soh/tools/zapd_to_torch.py` — extend injection for all types

### Torch factories (Phase 2)
- `src/factories/DisplayListFactory.cpp` — remove G_DL/G_MTX/G_MOVEMEM auto-discovery
- `src/factories/oot/OoTSceneFactory.cpp` — remove SetMesh/collision auto-discovery
- `src/factories/oot/OoTSkeletonFactory.cpp` — remove limb/DList auto-discovery
- `src/factories/oot/DeferredVtx.cpp` — may simplify or remove if all VTX pre-declared

### Torch core (Phase 3)
- `src/Companion.h` — remove AddAsset, RegisterAsset signatures
- `src/Companion.cpp` — remove AddAsset, RegisterAsset implementations

## Verification (per migration step)

1. Run `catalog_undeclared.py` to confirm 0 remaining undeclared assets of the migrated type
2. `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
3. Timing comparison vs baseline
4. Test multiple ROM versions to ensure enrichment is version-independent
