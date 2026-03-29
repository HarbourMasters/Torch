# OoT Remaining Work

## Current State: 34,602 / 35,386 (97.8%)

## Failures (46)

### Cutscene Data (44)
- 34 CutsceneData files from alternate headers — likely command parsing sync issues
  where our command-aware parser gets different offsets than ZAPDTR
- 8 named cutscenes (gXxxCs) — declared in YAML, generated but content differs
- 2 unnamed cutscenes — similar issues
- Root cause investigation needed: for hiral_demo cutscenes, the first command ID
  differs between ref and gen, suggesting either wrong cutscene pointer resolution
  or different command parsing producing shifted output
- Possible approaches:
  1. Port more of ZAPDTR's command parsing to handle edge cases
  2. Compare command-by-command to find which specific command type causes desync
  3. Check if named cutscenes from YAML are resolving to wrong ROM offsets

### Other (2)
- 1 pathway count issue (spot04_sceneSet_00D590) — neighbor-based count inference
- 1 version file — metadata mismatch (expected)

## Not Generated (738)

### Audio (598)
- No OOT:AUDIO factory — needs investigation of OoT audio format vs NAudio

### Scenes (135)
- ~35 backgrounds (JPEG data, needs BackgroundFactory)
- ~80 named cutscenes/paths (from YAML, need to be resolved by scene factory)
- ~20 scene-level DLists (declared in room XMLs, stripped during YAML conversion)

### Text (4)
- OoTTextFactory is a stub — needs reimplementation

### portVersion (1)
- Metadata file
