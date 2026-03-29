# Plan: OoT Scene/Room Factory

## Context

20,432 assets pass (objects, overlays, code), 0 failures. 14,954 assets remain ungenerated: 14,355 scenes, 598 audio, 1 portVersion. This plan covers scene support — the largest remaining gap.

Scene/Room assets in OTRExporter are complex structured binaries. The scene command table (8-byte entries in ROM) gets parsed and serialized into a binary format (resource type `OROM` = 0x4F524F4D) with string references to sub-assets (DLists, collision, paths, cutscenes, rooms). Sub-assets are emitted as separate O2R entries.

## Current State

- 489 scene/room YAML files exist at `soh/assets/yml/pal_gc/scenes/{dungeons,overworld,indoors,shops,misc}/`
- YAMLs contain: TEXTURE (3,292), OOT:SCENE (101), OOT:ROOM (388), OOT:CUTSCENE (73), OOT:PATH (28), GFX (20), BLOB (1)
- No factories registered for OOT:SCENE, OOT:ROOM, OOT:CUTSCENE, or OOT:PATH
- **Path mismatch**: YAML dirs use `scenes/dungeons/`, manifest expects `scenes/nonmq/` or `scenes/shared/`

## 14,355 Scene Assets Breakdown

| Type | Count | Source | Handling |
|------|-------|--------|----------|
| DLists | 5,839 | SetMesh polygon entries | Auto-discover via AddAsset (GFX factory) |
| VTX | 3,318 | Referenced by DLists | Auto-discovered by DList factory |
| Textures | 3,292 | Already in YAMLs | TEXTURE factory (already works) |
| Scene/Room | 527 | Main entries | **New OOT:SCENE factory** |
| Alt Headers | ~213 | SetAlternateHeaders | **New factory (recursive parse)** |
| ActorEntry | 568 | SetActorList | 0-byte companion files |
| Pathways | 171 | SetPathways | Companion files (Path format) |
| CollisionHeader | 101 | SetCollisionHeader | Auto-discover (OOT:COLLISION factory) |
| Cutscenes | 73 | SetCutscenes | Companion files (Cutscene format) |
| Backgrounds | 35 | SetMesh type 1 | BLOB companion files |
| Others | ~218 | Sets with cutscene data, etc. | Mixed |

## Critical Path Issue

OTRExporter's `GetPrefix()` maps scenes to `scenes/shared/` by default, and `scenes/nonmq/` for specific MQ-variant dungeons matching this regex:
```
(ydan|ddan|bdan|Bmori1|HIDAN|MIZUsin|jyasinzou|HAKAdan|HAKAdanCH|ice_doukutu|men|ganontika)
```

Torch derives output paths from YAML directory structure (`gCurrentDirectory`). The YAML files must be reorganized to match: `scenes/shared/` and `scenes/nonmq/`.

## Implementation Plan

### Phase 0: Fix Scene YAML Paths

**Modify `zapd_to_torch.py`**: Change `get_output_category()` for scene files to output `scenes/shared/` or `scenes/nonmq/` instead of `scenes/dungeons/` etc., matching OTRExporter's prefix logic.

Regenerate all scene YAMLs. Verify scene textures pass (3,292 assets should immediately start working).

### Phase 1: Scene/Room Factory — Core Structure

**Create `src/factories/oot/OoTSceneFactory.h`** and **`OoTSceneFactory.cpp`**

The factory handles both OOT:SCENE and OOT:ROOM (same binary format).

**Parse method**: Read 8-byte scene commands from ROM at the YAML offset until EndMarker (0x14). For each command, read `cmdID` (byte 0), `cmdArg1` (byte 1), `cmdArg2` (big-endian uint32 at bytes 4-7). Follow `cmdArg2` pointer to read command-specific data arrays.

**Export method**: Write `WriteHeader(OROM, 0)` + `uint32_t cmdCount` + per-command serialization matching `RoomExporter.cpp`.

**Command types to implement** (ordered by complexity):

Simple inline (data in the 8-byte command itself):
- 0x05 SetWind, 0x07 SetSpecialObjects, 0x08 SetRoomBehavior
- 0x10 SetTimeSettings, 0x11 SetSkyboxSettings, 0x12 SetSkyboxModifier
- 0x14 EndMarker, 0x15 SetSoundSettings, 0x16 SetEchoSettings
- 0x19 SetCameraSettings

Pointer-based arrays:
- 0x00 SetStartPositionList, 0x01 SetActorList — 16-byte actor entries
- 0x0E SetTransitionActorList — 16-byte entries
- 0x06 SetEntranceList — 2-byte entries, count needs inference
- 0x0B SetObjectList — 2-byte uint16 entries
- 0x0F SetLightingSettings — 22-byte entries
- 0x0C SetLightList — 14-byte entries
- 0x13 SetExitList — 2-byte entries, count needs inference
- 0x02 SetCsCamera — cameras + points arrays

Sub-asset referencing:
- 0x04 SetRoomList — write path strings to room files
- 0x03 SetCollisionHeader — AddAsset OOT:COLLISION + write path
- 0x0A SetMesh — AddAsset GFX for each opa/xlu DList + write paths
- 0x0D SetPathways — companion files with Path binary format
- 0x17 SetCutscenes — companion files with Cutscene binary format
- 0x18 SetAlternateHeaders — recursive parse for alt scene/room headers

**Register** in `Companion.cpp`:
```cpp
this->RegisterFactory("OOT:SCENE", std::make_shared<OoT::OoTSceneFactory>());
this->RegisterFactory("OOT:ROOM", std::make_shared<OoT::OoTSceneFactory>());
```

### Phase 2: Path Companion Files

In SetPathways handler: read pathway list pointer, for each pathway read numPoints + point data (int16 x,y,z). Serialize as companion file with `WriteHeader(OoTPath=0x4F505448, 0)` + `uint32 numPathways` + per-pathway `uint32 numPoints` + points.

### Phase 3: Cutscene Companion Files

In SetCutscenes handler: read cutscene pointer, copy raw uint32 words from ROM (the N64 cutscene format IS the OTR format). Wrap with `WriteHeader(OoTCutscene=0x4F435654, 0)` + size + data + CS_END markers.

### Phase 4: Empty ActorEntry Companion Files

The 568 ActorEntry manifest entries are 0-byte files. Emit via `RegisterCompanionFile(name, {})`.

## Key Reference Files

| File | Purpose |
|------|---------|
| `Shipwright/OTRExporter/OTRExporter/RoomExporter.cpp` (636 lines) | Reference binary format |
| `Shipwright/OTRExporter/OTRExporter/DisplayListExporter.cpp:996-1090` | Path prefix logic |
| `Shipwright/OTRExporter/OTRExporter/PathExporter.cpp` | Path binary format |
| `Shipwright/OTRExporter/OTRExporter/CutsceneExporter.cpp` | Cutscene binary format |
| `Torch/src/factories/oot/OoTCollisionFactory.cpp` | Closest existing pattern |
| `Torch/src/factories/oot/OoTSkeletonFactory.cpp` | AddAsset/companion file pattern |
| `Torch/soh/tools/zapd_to_torch.py` | YAML generation + path mapping |

## Files to Create/Modify

| File | Action |
|------|--------|
| `src/factories/oot/OoTSceneFactory.h` | **Create** |
| `src/factories/oot/OoTSceneFactory.cpp` | **Create** |
| `src/Companion.cpp` | **Modify** — register OOT:SCENE, OOT:ROOM |
| `soh/tools/zapd_to_torch.py` | **Modify** — fix scene path mapping |

## Verification

1. After Phase 0: `soh/test_assets.sh --category scenes` should show 3,292 texture passes
2. After Phase 1-4: incremental scene/room passes, target all 14,355
3. Full run: `soh/test_assets.sh --failures-only` should show 0 failures for scenes
