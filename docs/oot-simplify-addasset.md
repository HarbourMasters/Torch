
# Simplify OoT factories: remove dead auto-discovery code

## Context

With fully enriched YAML (zero AddAsset calls), the auto-discovery fallback
code in OoT factories is dead code. These fallbacks add complexity, make the
factories harder to understand, and mask enrichment bugs (silently creating
assets instead of erroring). This plan removes them.

## Changes

### 1. AddAsset: warn → throw for OoT (Companion.cpp)
The current AddAsset logs a warning when an undeclared asset is found.
Change to throw for OoT (GBIMinorVersion::OoT) so enrichment gaps are
caught immediately. Keep warn/create for other games.

### 2. Remove GFX auto-discovery from OoTSceneFactory (lines 152-170)
`ResolveGfxPointer()` calls AddAsset when a DList isn't found. With
enriched YAML, all scene DLists are declared. Change to just warn and
return empty string — the error will be caught by the caller.

Same for `ResolveGfxWithAlias()` which wraps it.

### 3. Remove collision auto-discovery from OoTSceneFactory (line ~493)
SetCollisionHeader creates OOT:COLLISION via AddAsset. With enrichment,
collision assets are pre-declared. Remove the AddAsset fallback.

### 4. Remove GFX auto-discovery from OoTSkeletonFactory (line ~70)
Skeleton limb DLists are auto-discovered via AddAsset. With enrichment,
they're pre-declared. Remove the fallback.

### 5. Remove limb auto-discovery from OoTSkeletonFactory (line ~389)
OOT:LIMB assets are auto-discovered during skeleton parsing. With
enrichment, all limbs are pre-declared. Remove the AddAsset fallback.

### 6. Remove limb table BLOB creation from OoTSkeletonFactory (line ~415)
BLOB limb table assets are created via AddAsset. With enrichment,
they're pre-declared. Remove.

### 7. Remove MTX auto-discovery from DisplayListFactory (line ~1017)
G_MTX handler creates MTX via AddAsset. With enrichment, MTX are
pre-declared. Remove.

### 8. Remove VTX auto-creation fallback from DisplayListFactory (line ~1098)
The `AddAsset(vtx)` call in the non-deferred path. With enrichment,
VTX are pre-declared. DeferredVtx::AddPending is still needed for
the deferred path (consolidation logic).

### 9. Simplify DeferredVtx::FlushDeferred (DeferredVtx.cpp)
FlushDeferred calls AddAsset for merged VTX groups. With enrichment,
the merged groups are already declared as OOT:ARRAY in YAML. AddAsset
finds them and early-returns. The FlushDeferred still needs to run for
overlap registration, but the AddAsset call could be replaced with a
GetNodeByAddr check + warn if not found.

### 10. Remove gSunDL VTX override (DisplayListFactory line ~321)
The special case for gSunDL that searches TEXTURE/BLOB for a covering
asset. With the gSunDL VTX declared in YAML, the normal G_VTX handler
finds it via GetNodeByAddr. The override is no longer needed.

### 11. Remove DList auto-discovery from DisplayListFactory (line ~868, 957)
G_DL and G_BRANCH_Z handlers create child DLists via AddAsset. With
enrichment, all child DLists are pre-declared. Remove.

### Items NOT removed
- Alternate header processing in OoTSceneFactory (line ~860) — Set_ entries
  are still created via AddAsset for non-enriched rooms (the invalid 28).
  Actually: those are filtered out. But the Set_ YAML files reference the
  parent as external, and the parent processes SetAlternateHeaders. The
  code at line 860 checks GetNodeByAddr and skips if found. Keep this
  as-is — it's already a no-op for enriched YAML.
- DeferredVtx infrastructure — still needed for overlap registration even
  if AddAsset calls are removed from FlushDeferred.
- RegisterAssetAlias — still needed for Set_ DList aliases.

## Files to modify
- `src/Companion.cpp` — AddAsset throw for OoT
- `src/factories/oot/OoTSceneFactory.cpp` — remove GFX + collision auto-discovery
- `src/factories/oot/OoTSkeletonFactory.cpp` — remove GFX + limb + BLOB auto-discovery
- `src/factories/DisplayListFactory.cpp` — remove gSunDL override, MTX/VTX/DList auto-discovery
- `src/factories/oot/DeferredVtx.cpp` — simplify FlushDeferred

## Follow-up items (not in this PR)

### Cartridge.cpp CRC BSWAP32 removal
- Removed `BSWAP32` from CRC read — likely correct (reader already Big endian)
  but other ports may rely on the old double-swapped value.
- No config info available at `Cartridge::Initialize()` time to guard this.
- Need to verify all other ports still match their expected CRC values,
  or add a guard (flag passed into Initialize, or post-init fixup).

### OoTAudioFactory.cpp
- parse() is 626 lines — split into helper methods:
  - `parseSequences()` (~80 lines)
  - `parseSamples()` (~180 lines)
  - `parseSoundFonts()` (~280 lines)
- readBE32/readBE16/readBEFloat: used for random-access big-endian reads from
  raw audio bank buffer. Create a full BinaryReader per call — inefficient but
  functional. Follow-up: deeper dive before changing (need to understand if a
  shared utility pattern makes sense across Torch)
- No AddAsset calls — clean

### OoTCollisionFactory.cpp
- Extract `if (camDataAddr != 0)` block (lines 103-223, 120 lines) into
  a `parseCameraData()` helper method

### OoTSceneFactory.cpp — split first, then clean up each
- **Step 1**: Split OoTPathFactory into own .cpp/.h
- **Step 2**: Split OoTCutsceneFactory + SerializeCutscene into own .cpp/.h
- **Step 3**: Clean up each file individually after split
- Notes for cleanup pass:
  - parse() is 700+ lines — extract SetMesh (~200 lines) and SetAlternateHeaders into helpers
  - ResolveGfxPointer has AddAsset auto-discovery — dead code with enrichment
  - SetCollisionHeader has AddAsset auto-discovery — dead code with enrichment

### OoTSkeletonFactory.cpp — split into separate files
- Split into `OoTSkeletonFactory.cpp/h` and `OoTLimbFactory.cpp/h`
- Currently one file with two factories + shared helpers
- Do another simplification pass after the split

### OoTTextFactory.cpp
- Remove stale TODO comment ("source was lost in filesystem damage" — already reimplemented)
- Extract `while(true)` message parsing loop into a `parseMessages()` helper method
  to make `parse()` easier to read

### MtxFactory.cpp — POTENTIAL REGRESSION → create OoTMtxFactory
- **Non-float export path changed**: old code wrote `mt.mint[i][j]` (16 × int16 = 32 bytes),
  new code writes `rawInts[i]` (16 × int32 = 64 bytes). Breaks non-OoT ports.
- **Fix**: Create `OoTMtxFactory.cpp/h` with the rawInts format. Revert shared
  MtxFactory.cpp to main. Register OoTMtxFactory for MTX when OoT is active
  (overrides shared factory).
- Same pattern as OoTArrayFactory isolating OoT-specific logic

### Companion.cpp
- Factory registration: clean, behind #ifdef OOT_SUPPORT
- Directory override + alias writing: general-purpose, fine as-is
- PatchVirtualAddr VRAM logic: OoT-specific (segment 0x80 preference, overlay
  aliasing) but in shared code. Already guarded by GBIMinorVersion::OoT check
  in some places but not all — verify guards are consistent
- GetNodeByAddr multi-segment fallback: same concern, has GBIMinorVersion::OoT
  check for the overlay segment scan
- AddAsset enrichment warning: currently just warns. Could make stricter for OoT
  (throw) to catch enrichment regressions
- ParseVersionString endianness byte: needed, no simplification

### BlobFactory.cpp — shared factory, OoT-specific behavior
- Empty blob returns nullopt (no header) to match OTRExporter's 0-byte limb tables
- This is in the shared BlobFactory — could affect other games if they have empty blobs
- Same pattern as MtxFactory: consider OoTBlobFactory or guard with GBIMinorVersion

### DisplayListFactory.cpp — heavily modified, OoT logic intertwined
- 732 lines changed, OoT-specific logic woven into shared command processing
- 7 `#ifdef OOT_SUPPORT` blocks in parser, but exporter changes have NO ifdefs
- **RemapSegmentedAddr**: OoT-only helper, no ifdef — could affect other games
- **SearchVtx**: added OOT:ARRAY + cross-segment — OoT logic in shared function
- **gSunDL override**: hardcoded string check in exporter — fragile, OoT-specific
- **G_BRANCH_Z/G_RDPHALF_1**: new opcode handling — may be needed by other games too?
- **Alias segment check in G_VTX**: OoT overlay-specific, no ifdef
- **AddAsset calls** (3): child DList, MTX, VTX — dead code with enrichment
- Too intertwined for a separate OoTDisplayListFactory — would duplicate most of the file
- Better approach: guard OoT-specific exporter logic behind GBIMinorVersion checks,
  remove dead AddAsset calls, and move gSunDL override to a callback/hook
- **#ifdef OOT_SUPPORT is misleading**: always compiled in with default build options.
  Replace with runtime GBIMinorVersion::OoT checks in shared factories.
  Keep #ifdef only for OoT-only factory files (OoTSceneFactory etc.)
- **Deferred**: needs design discussion on how to isolate OoT logic without
  duplicating the file (inheritance, composition, command handler hooks, etc.)
- Do last after all other cleanups

### DeferredVtx.cpp
- FlushDeferred calls AddAsset to register merged VTX — with enrichment, VTX
  are already declared and AddAsset early-returns. Replace with GetNodeByAddr
  + warn if not found. Overlap registration stays the same.

### OoTAnimationFactory.cpp — split into separate files
- 4 factories in one file: OoTAnimationFactory, OoTCurveAnimationFactory,
  OoTPlayerAnimationHeaderFactory, OoTPlayerAnimationDataFactory
- Split each into own .cpp/.h
- No AddAsset, no TODOs — clean otherwise

### OoTArrayFactory.cpp
- Extract VTX and Vec3s blocks into helper methods in both parse() and Export()
- parse() and Export() become type dispatchers

### Endianness boilerplate across all OoT factories
- 33 instances of `SetEndianness(Torch::Endianness::Big)` in OoT factories
- Every factory creates a BinaryReader from AutoDecode output and sets Big endian
- N64 is always big-endian — this should be set once, not per-factory
- Options: AutoDecode returns a pre-configured reader, or factory base class helper

### Decompressor.cpp cleanup
- YAZ0 AutoDecode case is copy-paste of YAY0/MIO0 case — should fall through
  (`case YAZ0:` added to the existing `case YAY0: case YAY1: case MIO0:` block)
- TranslateAddr: high segments (>= 0x80) are VRAM addresses shoehorned into the
  segment table. Should be handled in the VRAM path (path 2) not the segment path
  (path 1). Currently does a double-lookup and has a complex condition.

## Verification
1. `cmake --build build -j32`
2. `test_assets.py` → 35,386/35,386
3. Timing ~4-7s (should be same or slightly faster)
4. No AddAsset warnings in output
