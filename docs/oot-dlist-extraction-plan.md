# DisplayListFactory OoT extraction plan

## Context
DisplayListFactory.cpp has 622 lines of OoT-specific additions woven into shared
DList code, making the diff to main hard to review. We want to mechanically extract
all OoT-specific blocks into `src/factories/oot/OoTDListHelpers.h/.cpp`, replacing
each with a helper call. No logic changes, no consolidation — just isolation.

## Approach
Create `OoTDListHelpers.h/.cpp` with free functions (or a class) that
DisplayListFactory calls. Each extraction should be one commit so we can verify
with test_assets between steps.

## Files
- `src/factories/DisplayListFactory.cpp` — remove OoT blocks, add helper calls
- `src/factories/DisplayListFactory.h` — may need include update
- `src/factories/oot/OoTDListHelpers.h` — new, declarations
- `src/factories/oot/OoTDListHelpers.cpp` — new, implementations

## Extraction order (smallest/simplest first)

### Phase 1: Static helpers (no Export/parse signature dependency)
1. Move `RemapSegmentedAddr` to helpers file as-is
2. Move OoT `SearchVtx` — restore main's version in DisplayListFactory,
   put OoT version in helpers, call site dispatches

### Phase 2: Export handlers (each takes w0/w1/writer and returns bool for "handled")
3. gSunDL VTX override (the block before G_VTX handler)
4. G_VTX handler — extract entire OoT deviation (isAliasSegment, cross-file,
   OOT:ARRAY, virtual segment) into one helper
5. G_DL handler — extract segment 8-13 skip, RemapSegmentedAddr, cross-segment fallback
6. G_SETTIMG handler — extract sShadowMaterialDL hack, unresolved fallback
7. gSunDL SETTILE/LOADBLOCK fix
8. G_MTX handler — extract OOT:MTX chain + RemapSegmentedAddr
9. G_SETOTHERMODE_H LUT re-encoding
10. G_NOOP zeroing
11. Unhandled opcode zeroing
12. Light handling changes (restore log, verify OTR encoding move)

### Phase 3: Parse handlers
13. Remove dead `isAutogen`
14. Child DList symbol derivation + AddAsset skip → helper
15. Light AddAsset skip → helper
16. G_RDPHALF_1 auto-discovery + DeferredVtx scanning → helper
17. G_MTX auto-discovery → helper (or remove if debug-only)
18. G_VTX skipVtx + DeferredVtx + cross-segment → helper
19. Flush per-DList VTX → DeferredVtx helper

## Verification
After each extraction: `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64`
— 35,386 assets must pass.
