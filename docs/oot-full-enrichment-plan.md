# Full YAML Enrichment: Remove Auto-Discovery from Torch (OoT PoC)

## Context

Torch currently auto-discovers ~6,400 assets at runtime via `AddAsset` during factory
parsing (DLists, MTX, LIGHTS, scene meshes, skeleton limbs, etc.). This is expensive
(~8s of the 35s runtime) and creates naming conflicts for scene assets that appear
under multiple alternate header paths.

The goal: YAML is the single source of truth for all assets. Torch expects fully
enriched YAML and errors on undeclared references instead of silently creating them.
This eliminates auto-discovery overhead, resolves the duplicate-name scene issue,
and is the foundation for future codegen.

**Scope: OoT proof-of-concept.** AddAsset is called from 19+ factories across
all supported games (SM64, SF64, F-Zero, NAudio, etc.). AddAsset itself stays
in Torch for other games. The goal is that OoT processing never reaches AddAsset
at all — every OoT asset is pre-declared in YAML. If AddAsset is called during
OoT processing, that's a bug in the enrichment (incomplete YAML).

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

### Phase 2: Make AddAsset throw

Make AddAsset throw an exception with the undeclared asset's type, offset, and
the file being processed. This immediately surfaces every gap in enrichment:

```cpp
std::optional<YAML::Node> Companion::AddAsset(YAML::Node asset) {
    auto type = GetTypeNode(asset);
    auto offset = GetSafeNode<uint32_t>(asset, "offset");
    throw std::runtime_error(
        "AddAsset called for " + type + " at " + Torch::to_hex(offset, false) +
        " in " + this->gCurrentFile + " — YAML enrichment incomplete");
}
```

Then iteratively:
1. Run Torch on OoT ROM → hits throw
2. Identify which asset type/offset is missing from YAML
3. Fix enrichment tooling to pre-declare it
4. Repeat until 35,386/35,386 pass with no throws

### Phase 3: Clean up (after PoC validates)

Once OoT runs clean with AddAsset throwing:
- Remove the OoT-specific AddAsset call sites from factories (dead code)
- Remove `autogen` field handling for OoT path
- Simplify `ProcessFile` — no more recursive AddAsset → RegisterAsset → ParseNode chains
- Keep AddAsset for other games (restore non-throwing version behind a flag or game check)

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

## Known issues (from review)

### Critical

1. **DeferredVtx merges adjacent VTX arrays** — it doesn't just discover VTX, it
   consolidates adjacent arrays into larger merged groups. The VTX JSON from
   `extract_vtx.py` already declares the merged groups (it reads them from the
   reference O2R which has the post-merge result). But if DeferredVtx still runs
   and tries to merge pre-declared VTX, the behavior may differ. Options:
   - Keep DeferredVtx running (it will find pre-declared VTX via GetNodeByAddr
     and skip AddAsset) — check if merge logic still produces correct overlaps
   - Disable DeferredVtx when YAML is fully enriched (VTX overlaps would need
     to be pre-declared too)

2. **CRC64 path matching** — binary output hashes the full asset path string.
   Pre-declared assets must use the exact name/path that auto-discovery would
   produce. `catalog_undeclared.py` uses names from the reference O2R, which
   should match. Verify per-type.

3. **OoTSkeletonFactory recursive chains** — calls AddAsset for limbs inside its
   own parse(), then immediately uses the result (ResolvePointer needs the limb
   registered). Options:
   - Pre-declare all limbs in YAML (skeleton factory finds them via GetNodeByAddr)
   - Refactor skeleton factory to not depend on immediate parsing
   - Keep skeleton AddAsset calls as-is if limbs are already pre-declared
     (AddAsset early-returns, no overhead)

### Medium

4. **GetNodesByType skips `autogen` assets** (Companion.cpp:1836). Pre-declared
   assets won't have `autogen=true`, so they'll appear in GetNodesByType results.
   Callers: SearchVtx (DisplayListFactory), gSunDL override. Verify these don't
   break when auto-discovered assets become visible.

### Low

5. **Directory context** — scene room YAMLs set `:config: directory` which controls
   output paths. Pre-declared assets in the correct YAML file get the right
   directory. Enrichment tool must place assets in the right file.

6. **First-pass vs RegisterAsset** — first pass populates gAddrMap without parsing.
   AddAsset triggers immediate parsing via RegisterAsset→ParseNode. With enrichment,
   parsing happens in the normal second pass. Should be safe since no code depends
   on immediate mid-factory parsing (except OoTSkeletonFactory, see #3).

## Verification (per migration step)

1. Run `catalog_undeclared.py` to confirm 0 remaining undeclared assets of the migrated type
2. `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
3. Timing comparison vs baseline
4. Test multiple ROM versions to ensure enrichment is version-independent
