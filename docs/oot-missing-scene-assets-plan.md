# Plan: Fix 135 Missing Scene Sub-Assets

## Context
34,648/35,386 passing (97.9%), 0 failures, 738 not generated.
Of the 738, 135 are scene sub-assets. The rest: 598 audio (no factory), 4 text (stub factory), 1 portVersion.

## Breakdown of 135 Missing Scene Sub-Assets

| Type | Count | Root Cause |
|------|-------|------------|
| Named cutscenes (gXxxCs) | 49 | OOT:CUTSCENE type in YAML, no factory registered |
| Backgrounds | 35 | Not in YAML, needs mesh type 1 support in scene factory |
| Named paths (gXxxPath) | 28 | OOT:PATH type in YAML, no factory registered |
| Scene-level DLists | 20 | Stripped from YAML by zapd_to_torch.py room DList skip |
| VTX | 2 | Scene vertex data not auto-discovered |
| Misc | 1 | gLakeHyliaFireArrowsCS |

## Approach by Category

### 1. Named Cutscenes (49) — Register OOT:CUTSCENE factory
These are YAML-declared with `type: OOT:CUTSCENE` and an offset. The scene factory already creates cutscene companion files from SetCutscenes commands. The OOT:CUTSCENE factory just needs to:
- Read BE ROM data at the offset
- Use the same command-aware cutscene re-serialization (existing code in SetCutscenes handler)
- Write the same binary format (header + word count + CS data)

**Implementation**: Extract the cutscene serialization logic from the SetCutscenes handler into a reusable function. Register an OOT:CUTSCENE factory that calls it.

### 2. Named Paths (28) — Register OOT:PATH factory
These are YAML-declared with `type: OOT:PATH` and an offset. The scene factory already creates pathway companion files from SetPathways commands. The OOT:PATH factory just needs to:
- Read the pathway list at the offset (numPoints, pointsAddr entries)
- Write the same binary format (header + u32 count + per-pathway data)
- The YAML should include numPaths from the XML

**Implementation**: Extract the pathway serialization into a reusable function. Register an OOT:PATH factory. Check if the YAML has a `num_paths` attribute (from XML NumPaths).

### 3. Scene-Level DLists (20) — Fix zapd_to_torch.py
These were stripped by our rule "skip all DList entries in room files." But 20 DLists declared in room XMLs are scene-level (g-prefixed names like gKinsutaDL_). They should be kept.

**Implementation**: In zapd_to_torch.py, only skip DLists whose name starts with the room file name (e.g., `kinsuta_room_0DL_*`). Keep g-prefixed DLists.

NOTE: We tried this before and it caused conflicts with room mesh DLists at the same offsets. The scene-level DLists in room files happen to share offsets with room mesh DLists. Need to handle the naming conflict.

### 4. Backgrounds (35) — Implement mesh type 1 background handling
Backgrounds are JPEG-based room images used in mesh type 1 (prerendered rooms). The scene factory handles mesh type 1 partially but doesn't create background companion files.

**Implementation**: In the SetMesh handler's type 1 branch, create Background companion files containing the JPEG data from ROM. OTRExporter's format is: header (IGBO) + JPEG data.

### 5. VTX (2) + Misc (1) — Skip for now
These are edge cases that can be addressed later.

## Priority Order
1. Scene-level DLists (20) — Quick fix in zapd_to_torch.py (with conflict resolution)
2. Named cutscenes (49) — Extract and register factory
3. Named paths (28) — Extract and register factory
4. Backgrounds (35) — New mesh type 1 handling

## Files to Modify
- `soh/tools/zapd_to_torch.py` — Fix DList skip for scene-level DLists
- `src/factories/oot/OoTSceneFactory.cpp` — Extract cutscene/path serialization, add background support
- `src/factories/oot/OoTSceneFactory.h` — Add OOT:CUTSCENE and OOT:PATH factory classes
- `src/Companion.cpp` — Register new factories

## Verification
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --failures-only`
- Should recover ~132 of 135 missing scene assets (excluding 2 VTX + 1 misc)
- 0 regressions
