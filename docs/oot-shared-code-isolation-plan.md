# Isolate OoT-specific changes from shared Torch code

## Context
OoT support added ~1000 lines to shared factory files. Need to categorize what's general
vs OoT-specific and isolate the latter. Ghostship (SM64) repo available as sibling for
regression testing after isolation. Focus on isolation first, then test.

## Categorized changes (diff main...HEAD on shared files)

### Category A: General improvements (keep as-is)
1. `Companion.cpp`: graceful unknown type skip, directory override, asset aliases, portVersion fix
2. `Companion.h`: `GBIMinorVersion::OoT` enum
3. `BaseFactory.h`: `IS_VIRTUAL_SEGMENT` + `ASSET_PTR` update
4. `BlobFactory.cpp`: empty blob handling
5. `MtxFactory.cpp`: raw int32 MTX export (ZAPDTR-correct format)
6. `Decompressor.cpp`: YAZ0 support
7. `ResourceType.h`: OoT type IDs
8. `CMakeLists.txt`: `BUILD_OOT` + libyaz0
9. `DisplayListFactory.cpp`: G_BRANCH_Z/G_RDPHALF_1 GBI entries
10. `Cartridge.cpp`: CRC byte-swap fix

### Category B: Properly gated behind GBIMinorVersion::OoT (done)
1. gSunDL VTX override
2. sShadowMaterialDL BSS hack
3. gSunDL texture format hack

### Category C: Needs isolation

**C1. DeferredVtx namespace** (~100 lines in DisplayListFactory.cpp)
- VTX consolidation system called only by OoTSceneFactory
- Inert for other ports (no one calls BeginDefer) but pollutes shared file
- **Fix**: Move to `src/factories/oot/DeferredVtx.cpp/h`, keep declarations in
  DisplayListFactory.h since DList export references VTX overlap data

**C2. RemapSegmentedAddr()** (~30 lines in DisplayListFactory.cpp)
- Remaps segment addresses when overlays alias segments to same ROM data
- Called unconditionally in G_SETTIMG and G_MTX handlers
- Harmless no-op when no multi-segment overlap exists, but adds work
- **Fix**: Gate behind `GBIMinorVersion::OoT` — only call RemapSegmentedAddr when OoT minor

**C3. PatchVirtualAddr() rewrite** (~40 lines in Companion.cpp)
- Changed segment 0x80 preference, VRAM-to-segmented conversion
- Runs for all ports unconditionally
- **Fix**: The old behavior (simple subtract/add) should remain the default. Add the
  segment-preference logic only when `GBIMinorVersion::OoT` or when virtual addr map
  has multiple segments mapping to same physical address.

**C4. GetNodeByAddr() rewrite** (~50 lines in Companion.cpp)
- Cross-segment lookup and VRAM resolution for external files
- The cross-segment scan is OoT-specific (overlay segments)
- **Fix**: Gate the cross-segment scan behind OoT minor. Keep the external file VRAM
  resolution as general (it's useful for any port with VRAM overlays).

**C5. SearchVtx() changes** (DisplayListFactory.cpp)
- Added OOT:ARRAY type search alongside VTX
- **Fix**: Gate the OOT:ARRAY search behind OoT minor

## Implementation order
1. Move DeferredVtx to oot/ directory (C1)
2. Gate RemapSegmentedAddr calls behind OoT minor (C2)
3. Gate cross-segment scan in GetNodeByAddr (C4)
4. Gate SearchVtx OOT:ARRAY search (C5)
5. Conditionally apply PatchVirtualAddr segment preference (C3)
6. Build + test OoT: 35,386/35,386
7. Test SM64 with Ghostship for regressions

## Files to modify
- `src/factories/DisplayListFactory.cpp` — gate C2, C5; remove DeferredVtx body
- `src/factories/oot/DeferredVtx.cpp` (new) — moved from DisplayListFactory
- `src/factories/oot/DeferredVtx.h` (new) — declarations
- `src/Companion.cpp` — gate C3, C4
- `src/factories/DisplayListFactory.h` — update includes

## Verification
1. OoT: `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
2. SM64: Generate Ghostship O2R + manifest, verify match
