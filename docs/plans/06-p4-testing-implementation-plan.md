# P4 Testing Implementation Plan

## Overview

P4 covers game-specific factory data structures and algorithms that were not included in P1-P3: MK64 factories, F-Zero X factories (including checksum algorithms), PM64 data structures, SM64 AnimationData, and F-Zero SoundFont/Sequence types.

**Estimated tests:** ~55-65

---

## Commit 1: MK64 Factory Data Structures

**File:** `tests/MK64DataTest.cpp`

### CourseVtx (~4 tests)
- `CourseVtx` struct construction: `ob[3]`, `tc[2]`, `cn[4]` field access
- `CourseVtxData` with vector of vertices
- `CourseVtxFactory` exporters: Code, Header, Binary
- Manual parse: construct CourseVtx from big-endian bytes via BinaryReader (12 bytes per vertex)

**Source:** `src/factories/mk64/CourseVtx.h`

### TrackPath (~3 tests)
- `TrackPath` struct: `posX, posY, posZ` (int16_t), `trackSegment` (uint16_t) — 8 bytes
- `PathData` with vector of paths
- `PathFactory` exporters: Code, Header, Binary

**Source:** `src/factories/mk64/Paths.h`

### TrackSections (~3 tests)
- `TrackSections` struct: `crc` (uint64_t), `surfaceType`, `sectionId` (int8_t), `flags` (uint16_t)
- `TrackSectionsData` with vector
- `TrackSectionsFactory` exporters

**Source:** `src/factories/mk64/TrackSections.h`

### ActorSpawnData (~3 tests)
- `ActorSpawnData` struct: `x, y, z` (int16_t), `id` (uint16_t) — 8 bytes
- `SpawnDataData` with vector
- `SpawnDataFactory` exporters

**Source:** `src/factories/mk64/SpawnData.h`

### DrivingBehaviour (~3 tests)
- `BhvRaw` struct: `waypoint1, waypoint2` (int16_t), `bhv` (int32_t) — 8 bytes
- `DrivingData` with vector
- `DrivingBehaviourFactory` exporters

**Source:** `src/factories/mk64/DrivingBehaviour.h`

### ItemCurve (~2 tests)
- `ItemCurveData` with vector<uint8_t> (100 bytes for 10x10 probability array)
- `ItemCurveFactory` exporters

**Source:** `src/factories/mk64/ItemCurve.h`

---

## Commit 2: F-Zero X Ghost Record Checksums

**File:** `tests/FZXGhostRecordTest.cpp`

### Save_CalculateChecksum (~3 tests)
- Sum of bytes in buffer: known 4-byte buffer → expected uint16_t
- Empty buffer (size 0) → 0
- Single byte → that byte's value

### CalculateReplayChecksum (~3 tests)
- Packs replay bytes into big-endian int32s and sums them
- 4-byte replay: single word checksum
- 8-byte replay: sum of two words
- Non-aligned size (e.g. 5 bytes): partial last word not included (loop only adds when i==0 after increment)

### GhostMachineInfo (~2 tests)
- Struct construction with all 20 uint8_t fields
- sizeof check (20 bytes, tightly packed)

### GhostRecordData construction (~2 tests)
- Full construction with all fields
- Field access verification

### GhostRecordFactory exporters (~2 tests)
- Has Code, Header, Binary, Modding exporters
- SupportModdedAssets() returns true

**Source:** `src/factories/fzerox/GhostRecordFactory.h`, `.cpp`

---

## Commit 3: F-Zero X Course Data & Checksum

**File:** `tests/FZXCourseTest.cpp`

### CourseRawData struct (~2 tests)
- sizeof(CourseRawData) = 0x7E0 (2016 bytes)
- sizeof(ControlPoint) = 0x14 (20 bytes)

### CourseData construction (~2 tests)
- Construct with minimal control points, verify fields
- Empty control points vector

### CalculateChecksum (~3 tests)
- Empty course (0 control points) → checksum equals 0 (size of empty vector)
- Single control point with all zeros → deterministic result
- Checksum is deterministic (same input → same output)

### CourseFactory exporters (~2 tests)
- Has Code, Header, Binary, Modding exporters
- SupportModdedAssets() returns true

**Source:** `src/factories/fzerox/CourseFactory.h`, `.cpp`, `course/Course.h`

---

## Commit 4: F-Zero X EAD Animation & Limb, SoundFont Types

**File:** `tests/FZXDataTest.cpp`

### EADAnimationData (~2 tests)
- Construction with all 8 fields (frameCount, limbCount, 6 offset pointers)
- EADAnimationFactory exporters: Code, Header, Binary (no Modding)

### EADLimbData (~3 tests)
- Construction: dl, scale (Vec3f), pos (Vec3f), rot (Vec3s), nextLimb, childLimb, associatedLimb, associatedLimbDL, limbId
- Verify Vec3f/Vec3s field access
- EADLimbFactory exporters: Code, Header, Binary (no Modding)

### SoundFont types (~5 tests)
- FZX::TunedSample: sampleRef (string) + tuning (float)
- FZX::Drum: adsrDecayIndex, pan, isRelocated, tunedSample, envelopeRef
- FZX::Instrument: normalRangeLo/Hi, 3 TunedSamples
- FZX::AdpcmBook: order, numPredictors, book vector
- FZX::AdpcmLoop: start, end, count, predictorState vector
- FZX::SoundFontData: entries vector, mSupportSfx flag
- SoundFontFactory exporters: Code, Header, Binary

### Sequence types (~3 tests)
- SeqCommand: cmd, pos, state, channel, layer, size, args vector; operator< comparison
- SeqLabelInfo construction
- SequenceData: cmds, labels, hasFooter
- SequenceFactory exporters: Code, Header, Binary

**Source:** `src/factories/fzerox/EADAnimationFactory.h`, `EADLimbFactory.h`, `SoundFontFactory.h`, `SequenceFactory.h`

---

## Commit 5: PM64 Data Structures & SM64 AnimationData

**File:** `tests/PM64DataTest.cpp` and `tests/SM64AnimationTest.cpp`

### PM64ShapeData (~3 tests)
- Construction with buffer, display lists, vtxTableOffset, vtxDataSize
- PM64DisplayListInfo: offset + commands vector
- PM64ShapeFactory exporters: Header, Binary (no Code, no Modding)

### PM64EntityGfxData (~3 tests)
- Construction with buffer and display lists
- PM64EntityDisplayListInfo: offset + commands
- mStandaloneMtx vector (initially empty from constructor)
- PM64EntityGfxFactory exporters: Header, Binary

### SM64 AnimationData (~4 tests)
- Construction with all fields: flags, animYTransDivisor, startFrame, loopStart, loopEnd, unusedBoneCount, length, indices, entries
- ANIMINDEX_COUNT macro: `((boneCount + 1) * 6)` — verify for known bone counts
- AnimationFactory exporters: Binary only (no Code, Header, or Modding)
- Indices and entries vector access

**Source:** `src/factories/pm64/ShapeFactory.h`, `EntityGfxFactory.h`, `src/factories/sm64/AnimationFactory.h`

---

## Critical Files

| File | Role |
|---|---|
| `tests/MK64DataTest.cpp` | New — MK64 factory data structures |
| `tests/FZXGhostRecordTest.cpp` | New — F-Zero ghost record checksums |
| `tests/FZXCourseTest.cpp` | New — F-Zero course data & checksum |
| `tests/FZXDataTest.cpp` | New — F-Zero EAD, SoundFont, Sequence types |
| `tests/PM64DataTest.cpp` | New — PM64 shape & entity gfx data |
| `tests/SM64AnimationTest.cpp` | New — SM64 AnimationData |
| `src/factories/mk64/*.h` | Read-only reference |
| `src/factories/fzerox/*.h` | Read-only reference |
| `src/factories/pm64/ShapeFactory.h` | Read-only reference |
| `src/factories/pm64/EntityGfxFactory.h` | Read-only reference |
| `src/factories/sm64/AnimationFactory.h` | Read-only reference |

## Verification

After each commit:
```bash
cmake -B build -DBUILD_TESTS=ON && cmake --build build && build/torch_tests
```
