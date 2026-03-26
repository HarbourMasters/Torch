# P2 Testing Implementation Plan

## Context

P0 tests (88 tests) and P1 tests (48 tests) are complete, totaling 136 tests across 10 test suites. This plan covers P2: simple factories (MtxFactory, FloatFactory, LightsFactory, ViewportFactory), SM64 complex factories (GeoLayout, Collision, BehaviorScript), and Companion factory registration/lookup.

Key findings:
- MtxFactory, FloatFactory, LightsFactory, and ViewportFactory all have parse() methods that only depend on Decompressor::AutoDecode — their data structures and exporters can be tested in isolation, and parse logic can be replicated manually with BinaryReader.
- SM64 CollisionFactory::parse() has no Companion dependency and uses a command-based binary format with vertices, surfaces, special objects, and environment regions.
- SM64 GeoLayoutFactory and BehaviorScriptFactory parse() methods call RegisterAutoGen/GetNodeByAddr only during export, not parsing. The command parsing itself can be tested by constructing binary buffers.
- CommandMacros.h defines `CMD_PROCESS_OFFSET(offset)` which simplifies to identity for 4-byte-aligned offsets when `sizeof(uint32_t) >> 3 == 0` (i.e., CMD_SIZE_SHIFT = 0).
- Companion::RegisterFactory/GetFactory is a simple map insert/lookup that can be tested by constructing a Companion instance if the constructor doesn't have heavy side effects, or by testing the pattern in isolation.

---

## Commit 1: ViewportFactory tests

**File:** `tests/ViewportFactoryTest.cpp`

Simplest factory — reads 8 int16_t values into VpRaw (vscale[4] + vtrans[4]).

- `VpRaw` struct size is 16 bytes
- `VpData` construction with known VpRaw, verify field access
- `ViewportFactory::GetExporters()` has Code, Header, Binary
- Manual viewport parse from big-endian buffer: construct 16-byte buffer with known vscale/vtrans values, read with BinaryReader, verify all 8 fields

---

## Commit 2: LightsFactory tests

**File:** `tests/LightsFactoryTest.cpp`

Reads 24 bytes directly into Lights1Raw struct (AmbientRaw + LightRaw).

- `Lights1Raw` struct size is 24 bytes
- `LightsData` construction with known Lights1Raw, verify ambient and diffuse RGB, direction
- `LightsFactory::GetExporters()` has Code, Header, Binary
- Manual lights parse: construct 24-byte buffer, memcpy into Lights1Raw, verify all fields (ambient col[3], diffuse col[3], dir[3])

---

## Commit 3: FloatFactory tests

**File:** `tests/FloatFactoryTest.cpp`

Reads N big-endian floats into a vector.

- `FloatData` construction with known float vector, verify field access
- `FloatFactory::GetExporters()` has Code, Header, Binary
- Manual float parse: construct buffer with known big-endian IEEE 754 floats, read with BinaryReader, verify values
- Edge cases: zero, negative, very small/large floats

---

## Commit 4: MtxFactory tests

**File:** `tests/MtxFactoryTest.cpp`

Reads 4x4 fixed-point matrix (32 uint16_t values: 16 integer parts + 16 fractional parts) and converts to float.

- `MtxRaw` struct: verify it contains float mtx[16] and MtxS mt
- `MtxData` construction with known MtxRaw vector
- `MtxFactory::GetExporters()` has Code, Header, Binary
- Fixed-point conversion test: verify `FIXTOF((int32_t)((int_part << 16) | frac_part))` produces expected float for known values (identity matrix, known scale values)
- Manual matrix parse: construct 64-byte big-endian buffer (32 uint16_t values), read with BinaryReader, verify float conversion

---

## Commit 5: SM64 CollisionFactory data structures

**File:** `tests/SM64CollisionFactoryTest.cpp`

- `CollisionVertex` construction, verify x/y/z fields
- `CollisionTri` construction, verify x/y/z/force fields
- `CollisionSurface` construction with SurfaceType and tri vector
- `SpecialObject` construction with preset, position, and extra params
- `EnvRegionBox` construction with all 6 fields
- `Collision` default construction (empty vectors)
- `CollisionFactory::GetExporters()` has Code, Header, Binary

---

## Commit 6: SM64 CollisionFactory specialPresetMap

**File:** `tests/SM64CollisionFactoryTest.cpp` (append)

The static `specialPresetMap` maps 97 preset IDs to `SpecialPresetTypes`. Test representative entries:
- Known SPTYPE_YROT_NO_PARAMS entries (e.g., preset 0x00)
- Known SPTYPE_PARAMS_AND_YROT entries
- Known SPTYPE_NO_YROT_OR_PARAMS entries
- Verify map size

---

## Commit 7: SM64 GeoLayout data structures

**File:** `tests/SM64GeoLayoutTest.cpp`

- `GeoCommand` construction with opcode and argument vector
- `GeoLayout` construction with command vector
- `GeoLayoutFactory::GetExporters()` has Code, Header, Binary
- `GeoArgument` variant: verify storage/retrieval of different types (uint8_t, int16_t, uint32_t, Vec3f, Vec3s, std::string)

---

## Commit 8: SM64 BehaviorScript data structures

**File:** `tests/SM64BehaviorScriptTest.cpp`

- `BehaviorCommand` construction with opcode and argument vector
- `BehaviorScriptData` construction with command vector
- `BehaviorScriptFactory::GetExporters()` has Code, Header, Binary
- `BehaviorArgument` variant: verify storage/retrieval of different types (uint8_t, int16_t, uint32_t, int32_t, float)

---

## Commit 9: CommandMacros tests

**File:** `tests/CommandMacrosTest.cpp`

The macros in `n64/CommandMacros.h` are used by GeoLayout, BehaviorScript, and LevelScript parsers.

- `_SHIFTL(v, s, w)`: verify known shift-left-and-mask operations
- `_SHIFTR(v, s, w)`: verify known shift-right-and-mask operations
- `CMD_SIZE_SHIFT`: verify equals 0 (sizeof(uint32_t) >> 3)
- `CMD_PROCESS_OFFSET(offset)`: verify identity behavior for small offsets (0-15)
- `CMD_BBH(a, b, c)`: verify packing of 2 bytes + 1 halfword
- `CMD_HH(a, b)`: verify packing of 2 halfwords
- `CMD_W(a)`: verify identity

---

## Commit 10: Companion RegisterFactory/GetFactory

**File:** `tests/CompanionFactoryRegistrationTest.cpp`

Test the factory registration dispatch mechanism. Companion construction requires filesystem paths and YAML config, so we test the pattern rather than the full singleton:

- Construct a simple BaseFactory subclass for testing
- If Companion can be instantiated minimally: register factory, look it up by type, verify it returns the same factory
- If not: test that the gFactories map pattern works (register, lookup, missing key returns nullopt)
- Verify known factory type strings match expected factory classes (spot-check from the registration list)

**Note:** This may require a minimal Companion construction or extracting the map logic. If Companion construction has heavy side effects (loading ROM, etc.), we'll test indirectly by verifying factory GetExporters/GetAlignment for known types.

---

## Notes on Decompressor::AutoDecode dependency

All factory parse() methods call `Decompressor::AutoDecode(node, buffer)` which requires `Companion::Instance->GetCurrCompressionType()`. Rather than initializing Companion, we replicate the parsing logic manually using BinaryReader on raw (uncompressed) buffers. This tests the same field reading and data construction without the decompression layer.

---

## Critical Files

| File | Role |
|---|---|
| `tests/ViewportFactoryTest.cpp` | New — viewport data structure tests |
| `tests/LightsFactoryTest.cpp` | New — lights data structure tests |
| `tests/FloatFactoryTest.cpp` | New — float array tests |
| `tests/MtxFactoryTest.cpp` | New — matrix fixed-point conversion tests |
| `tests/SM64CollisionFactoryTest.cpp` | New — collision data structure tests |
| `tests/SM64GeoLayoutTest.cpp` | New — geo layout data structure tests |
| `tests/SM64BehaviorScriptTest.cpp` | New — behavior script data structure tests |
| `tests/CommandMacrosTest.cpp` | New — command macro tests |
| `tests/CompanionFactoryRegistrationTest.cpp` | New — factory registration tests |
| `src/factories/ViewportFactory.h/cpp` | Read-only reference |
| `src/factories/LightsFactory.h/cpp` | Read-only reference |
| `src/factories/FloatFactory.h/cpp` | Read-only reference |
| `src/factories/MtxFactory.h/cpp` | Read-only reference |
| `src/factories/sm64/CollisionFactory.h/cpp` | Read-only reference |
| `src/factories/sm64/GeoLayoutFactory.h/cpp` | Read-only reference |
| `src/factories/sm64/BehaviorScriptFactory.h/cpp` | Read-only reference |
| `src/n64/CommandMacros.h` | Read-only reference |
| `src/Companion.h/cpp` | Read-only reference |

## Verification

After each commit:
```bash
cmake -B build -DBUILD_TESTS=ON && cmake --build build && build/torch_tests
```
