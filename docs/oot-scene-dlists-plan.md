# Plan: Fix 20 Missing Scene-Level DLists (Updated)

## Context
20 g-prefixed DLists are missing from the O2R. They're declared in room XML files at unique offsets (different from room mesh DLists). Example: `gKinsutaDL_0030B0` at 0x30B0, while room mesh DLists are at 0x2BC8, 0x0CF0, etc.

## Investigation Results

### YAML Approach FAILED (838 regressions)
Adding g-prefixed DLists to YAML causes the GFX factory to process them during YAML parse. This triggers side effects (DeferredVtx state, VTX auto-discovery, segment state) that corrupt subsequent scene factory processing. 10 scenes affected across 838 assets.

### Root Cause of Conflicts
When a GFX (DList) asset is parsed from YAML:
1. GFX factory's DList parser runs
2. DList parser calls `DeferredVtx::BeginDefer()` (if scene context)
3. VTX auto-discovery creates VTX entries in the address map
4. Cross-segment resolution modifies segment state
5. When the scene factory later processes SetMesh, the state is corrupted

### All 20 DLists (18 from room files, across 10 scenes)
```
kinsuta: gKinsutaDL_0030B0, gKinsutaDL_00B088
men: gMenDL_008118, gMenDL_00FF78
spot01: gSpot01DL_009E38
spot02: gSpot02DL_0026D0
spot03: gSpot03DL_0074E8
spot04: gSpot04DL_002BB8, gSpot04DL_005058
spot05: gSpot05DL_009A60, gSpot05DL_009EE0
spot06: gSpot06DL_00A400, gSpot06DL_00A608
spot09: gSpot09DL_007108, gSpot09DL_008780
spot20: gSpot20DL_005E50, gSpot20DL_0066B8
miharigoya: gMiharigoyaDL_003DA0
```
Plus 2 more discovered from spot00 and spot16 room files.

## Recommended Approach: Post-processing companion files

After the scene factory finishes processing a room's YAML file (all SetMesh, SetActorList, etc are done), check if there are any unprocessed GFX-type YAML entries in the current file. For each one:
1. Process it as a GFX asset using `AddAsset` — but ONLY after all scene commands are processed
2. This avoids the DeferredVtx corruption because scene processing is complete

### Implementation
In `OoTSceneFactory.cpp`, after the main command processing loop and deferred alt header processing, check for remaining GFX assets in the current YAML file that haven't been processed yet. Process them last.

Alternatively: use `RegisterCompanionFile` to create these DLists manually during the scene factory's export phase, reading the DList data and running it through the DList binary exporter.

### Simplest Alternative: Script-based post-processing
Add a post-processing step in `test_assets.py` or a new script that:
1. Identifies which g-prefixed DLists are missing
2. Extracts them from the ROM using Torch's DList factory
3. Adds them to the O2R

This avoids modifying the scene factory entirely.

## Decision
Given the complexity and risk of modifying scene factory processing order, these 20 DLists are documented as a known gap. They represent 0.06% of total assets and can be addressed in a future refactor of the scene factory's processing model.

## Files Referenced
- `soh/tools/zapd_to_torch.py` — DList skip logic
- `src/factories/oot/OoTSceneFactory.cpp` — Scene factory processing
- `src/factories/DisplayListFactory.cpp` — DeferredVtx state management
- Reference O2R manifest — confirms g-prefixed names at unique offsets
