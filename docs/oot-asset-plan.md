# Plan: Get All OoT Assets Working in Torch

## Context

We're replacing Shipwright's ZAPDTR/OTRExporter with Torch for OoT asset extraction. The scaffolding is complete (DMA tables, verification scripts, 2 blobs verified). Now we need to implement support for all ~35,386 assets in the reference O2R. This requires both new OoT-specific factories in Torch and YAML configs for every asset.

## Strategy

Two parallel workstreams:
1. **Factories** — implement Torch factories for each OoT asset type
2. **YAMLs** — convert Shipwright's XML configs to Torch YAML format (scripted, not manual)

Order by: existing factory reuse first, then new factories by asset count (biggest impact first), deferring complex/unknown types.

## Phase 0: Foundation

### 0.1: Build system setup
- Add `BUILD_OOT` option to `CMakeLists.txt` (follow SM64/SF64 pattern)
- Add `OOT_SUPPORT` define
- Create `src/factories/oot/` directory
- Add filter regex for oot factories

### 0.2: Resource types
- Add OoT-specific type codes to `src/factories/ResourceType.h`
- Codes needed (from OTRExporter): OSKL, OSLB, OANM, OROM, OCOL, OCVT, OPTH, OPAM, OTXT, OAUD, OSFT, OSMP, OSEQ

### 0.3: XML-to-YAML conversion script
- Python script: reads XML + `soh/dma/pal_gc.json` → outputs YAML
- Lives at `soh/tools/xml_to_yaml.py` (or similar)
- Run incrementally as each phase adds type support
- Maps XML types to Torch YAML types, inserts DMA phys_start as segment offset

## Phase 1: Generic Types (~22,000 assets, ~62%)

Uses existing Torch factories. Covers: TEXTURE (8,989), GFX/DList (3,443), VTX (81), MTX (2), BLOB (62), ARRAY (85).

### Key risk: VTX format mismatch
Reference O2R uses `ResourceType::Array (0x4F415252)` with array_type=25 for vertex data — NOT `ResourceType::Vertex (0x4F565458)` or `GenericArray (0x47415252)`. Need to verify which type code the existing VTX/Array factories produce and adjust.

### Key risk: GFX auto-discovery
The DListFactory auto-discovers VTX/TEXTURE/LIGHTS from display list parsing. Must verify auto-discovered assets match reference paths and hashes. Test with a small object file first.

### Steps
1. Generate YAMLs for `textures/` (~151 files, ~2,000 assets) — TEXTURE only
2. Verify a batch against manifest
3. Generate YAMLs for `objects/` (~381 files) — GFX + TEXTURE + VTX + BLOB
4. Handle auto-discovery: GFX factory finds VTX/TEX/LIGHTS from display lists
5. Generate YAMLs for `overlays/` (~32 files) and `code/` (~4 files)
6. Verify

## Phase 2: Skeleton System (~4,076 assets)

New factories needed: OoT Limb, Skeleton, Animation.

### 2.1: OoT Limb Factory (`src/factories/oot/LimbFactory.cpp`)
- Type: OSLB (0x4F534C42)
- Limb types: Standard, LOD
- Parses: joint position (Vec3s), child/sibling indices, dList pointer
- Auto-discovers DLists referenced by limbs
- ~2,723 assets

### 2.2: OoT Skeleton Factory (`src/factories/oot/SkeletonFactory.cpp`)
- Type: OSKL (0x4F534B4C)
- Skeleton types: Normal, Flex
- Contains string paths to limb files
- ~197 assets

### 2.3: OoT Animation Factory (`src/factories/oot/AnimationFactory.cpp`)
- Type: OANM (0x4F414E4D)
- Standard animations + LegacyAnimation (12) + CurveAnimation (4)
- ~1,156 assets

### Steps
1. Study OTRExporter's skeleton/limb/animation exporters for binary format
2. Implement Limb factory first (standalone, simplest)
3. Implement Skeleton factory (references Limbs)
4. Implement Animation factory
5. Generate YAMLs for object files that contain skeleton/limb/animation entries
6. Verify against manifest

## Phase 3: Scene/Room System (~14,355 assets)

Most complex phase. New factories: Scene/Room, Collision, Path, Cutscene.

### 3.1: OoT Collision Factory (`src/factories/oot/CollisionFactory.cpp`)
- Type: OCOL (0x4F434F4C)
- Vertices, polygons, surface types, camera data, waterboxes
- ~203 assets

### 3.2: OoT Path Factory (`src/factories/oot/PathFactory.cpp`)
- Type: OPTH (0x4F505448)
- Array of Vec3s waypoints
- ~28 assets

### 3.3: OoT Cutscene Factory (`src/factories/oot/CutsceneFactory.cpp`)
- Type: OCVT (0x4F435654)
- Command stream: camera, actor actions, text boxes
- ~73 assets

### 3.4: OoT Scene/Room Factory (`src/factories/oot/SceneRoomFactory.cpp`)
- Type: OROM (0x4F524F4D) for both scenes and rooms
- Command-based format with headers, alternate headers
- References collision, paths, cutscenes as sub-assets
- Rooms contain DLists, VTX, textures
- ~489 assets (101 scenes + 388 rooms)
- Scenes also generate many auto-discovered assets (textures in rooms, etc.)

### Steps
1. Start with Collision and Path (simpler, standalone)
2. Implement Cutscene
3. Implement Scene/Room (depends on Collision, Path, Cutscene)
4. Start with a simple scene (few rooms, no alternate headers)
5. Handle alternate header sets
6. Generate scene YAMLs (complex: multi-segment configs)
7. Verify

## Phase 4: PlayerAnimation (~573 assets)

### 4.1: OoT PlayerAnimation Factory (`src/factories/oot/PlayerAnimFactory.cpp`)
- Type: OPAM (0x4F50414D)
- Raw frame data for Link's animations
- All from one DMA file (`link_animetion`)
- May be implementable as BLOB-like factory with OPAM type code

## Phase 5: Audio (~598 assets)

**Investigation needed first.** OoT audio uses different type codes and format version (v2) from NAudio v0/v1. Cannot reuse existing NAudio factories.

### 5.1: OoT Audio Root — Type OAUD, 1 asset
### 5.2: OoT SoundFont — Type OSFT, 38 assets
### 5.3: OoT Sample — Type OSMP, 459 assets
### 5.4: OoT Sequence — Type OSEQ, 110 assets

Strategy: reference Shipwright importers (`AudioSampleFactory.cpp`, `AudioSoundFontFactory.cpp`, `AudioSequenceFactory.cpp`) and OTRExporter serializers for binary format spec.

## Phase 6: Text (~6 assets)

### 6.1: OoT Text Factory (`src/factories/oot/TextFactory.cpp`)
- Type: OTXT (0x4F545854)
- Message table + message data per language
- 6 assets total

## Phase 7: Remaining (~3 assets)

- SYMBOL (1) → BLOB
- LIMBTABLE (1) → simple array or blob
- version/portVersion (2) → static metadata

## Key Files

- `src/factories/ResourceType.h` — add OoT type codes
- `src/Companion.cpp` — register OoT factories (lines 215-226 pattern)
- `CMakeLists.txt` — add BUILD_OOT option
- `src/factories/oot/` — new directory for all OoT factories
- `src/factories/DisplayListFactory.cpp` — GFX auto-discovery (verify OoT compat)
- `soh/dma/pal_gc.json` — ROM offsets for YAML generation
- OTRExporter source at `~/code/claude/Shipwright/OTRExporter/OTRExporter/` — serializers (write binary format)
- Shipwright importers at `~/code/claude/Shipwright/soh/soh/resource/importer/` — deserializers (read binary format back, effectively a spec)
- Shipwright type defs at `~/code/claude/Shipwright/soh/soh/resource/type/` — struct definitions and `SohResourceType.h` for type codes

## Verification

Each phase verified incrementally:
- Per-asset: `./soh/check.sh <rom> <asset-path>` against manifest
- Per-category: batch check all assets of a type
- Full: `./soh/verify.sh` for complete O2R comparison
- Target: all 35,386 files match reference SHA256 hashes
