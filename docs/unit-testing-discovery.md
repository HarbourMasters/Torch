# Torch Unit Testing Discovery Document

## Context

Torch is a C++20 ROM asset extraction and conversion tool for N64 games (SM64, MK64, SF64, PM64, F-Zero X, Mario Artist, NAudio). It processes ROM cartridge data through a factory pattern into multiple output formats (OTR/O2R binary archives, C code, C headers, XML). The goal is to discover all meaningful testing opportunities before setting up a unit test framework and writing any tests.

---

## Current Test Infrastructure

- **Test directory:** `test/` — contains YAML config fixtures for SM64, MK64, SF64 (no C++ tests exist)
- **Test hash file:** `test/torch.hash.yml` — hash verification data for assets
- **Test config:** `test/config.yml`
- **No existing C++ unit test framework** is set up

The CMakeLists.txt does not include a test target or any test framework (Google Test, Catch2, etc.).

---

## Codebase Summary

| Area | Files | Scale |
|---|---|---|
| Core orchestration | `src/Companion.h/cpp` | ~2,000 LOC |
| Factory classes | `src/factories/` | ~71 factories |
| N64 ROM handling | `src/n64/` | Cartridge, GBI macros |
| Utilities | `src/utils/` | Decompressor, TextureUtils, etc. |
| Data types | `src/types/` | Vec3D variants, RawBuffer |
| Binary I/O lib | `lib/binarytools/` | BinaryReader, BinaryWriter |
| Compression libs | `lib/libmio0/`, `lib/libyay0/` | MIO0, YAY0 |

---

## Testing Opportunities by Category

### 1. Binary I/O (`lib/binarytools/`)

**Files:** `BinaryReader.h/cpp`, `BinaryWriter.h/cpp`, `MemoryStream.h`

**What to test:**
- `BinaryReader`: reads `int8/16/32`, `uint8/16/32/64`, `float`, `double`, `string`, `CString` from known byte buffers
- `BinaryWriter`: writes each type and produces expected byte sequence
- Endianness handling: big-endian vs. little-endian byte swapping
- Seek operations (forward, backward, absolute)
- Round-trip: write then read back should produce original values
- Edge cases: empty buffer, reading past end, zero-length strings

**Why this matters:** Every factory depends on BinaryReader for parsing ROM data. Correctness here underpins all other tests.

---

### 2. Data Types (`src/types/`)

**Files:** `Vec3D.h/cpp`, `RawBuffer.h`, `SegmentedAddr.h`

**What to test:**
- `Vec3f`, `Vec3s`, `Vec3i`, `Vec3iu`, `Vec2f`, `Vec4f`, `Vec4s`: construction, field access, stream output
- Width/precision calculation methods used in code generation
- `RawBuffer`: construction from `vector<uint8_t>`, size tracking
- `SegmentedAddr`: segmented vs. physical address distinction, segment extraction

**Why this matters:** These types are used throughout all factory and exporter code.

---

### 3. N64 Utilities (`src/utils/`, `src/n64/`)

**Files:** `TorchUtils.h`, `TextureUtils.h/cpp`, `StringHelper.h/cpp`, `Cartridge.h/cpp`, `CommandMacros.h`

**What to test:**
- `to_hex<T>()`: int → "0x..." string for various types and values
- `contains<Container, Key>()`: compatibility wrapper
- `CalculateTextureSize()`: for each `TextureType` enum value and various dimensions (RGBA32, RGBA16, IA8, I4, CI4, etc.)
- `alloc_ia8_text_from_i1()`: 1-bit → 8-bit grayscale conversion
- `Cartridge`: parsing a synthetic ROM header produces correct title, country code, CRC, version
- `_SHIFTL`/`_SHIFTR` macros: bit shifting behavior
- `CMD_BBBB`, `CMD_BBH`, `CMD_HH`: command packing output

---

### 4. Decompressor (`src/utils/Decompressor.h/cpp`)

**Files:** `Decompressor.h/cpp`, `lib/libmio0/`, `lib/libyay0/`

**What to test:**
- `GetCompressionType()`: magic byte detection for MIO0, YAY0, YAY1, YAZ0, and raw (no magic)
- `Decode()`: known-good compressed buffers decompress to expected output
- `AutoDecode()`: pulls compression type from YAML and delegates correctly
- `IsSegmented()`: distinguish segmented from physical N64 addresses
- `TranslateAddr()`: with known segment table, convert segmented → physical
- Cache behavior: second call returns cached result without re-decompressing
- `ClearCache()`: cache is empty after clear

---

### 5. Base Factory Pattern (`src/factories/BaseFactory.h`)

**Files:** `BaseFactory.h`, `ResourceType.h`

**What to test:**
- `GetExporter(ExportType)`: returns correct exporter type for each export mode
- `GetAlignment()`: returns expected alignment for each factory
- `SupportModdedAssets()`: returns true/false per factory
- `HasModdedDependencies()`: returns true/false per factory
- `WriteHeader()`: produces correctly structured resource header bytes

---

### 6. Vertex Factory (`src/factories/VtxFactory.h/cpp`)

**What to test:**
- `VtxFactory::parse()`: known byte sequence → `VtxData` with correct `VtxRaw` fields
  - Position `ob[3]`, texture coords `tc[2]`, color+alpha `cn[4]`, flag
- Alignment = 8
- Code exporter: generates correct C array syntax for known vertex data
- Header exporter: generates correct `extern` declaration
- Binary exporter: round-trip `VtxRaw` → binary → `VtxRaw`

---

### 7. Display List Factory (`src/factories/DisplayListFactory.h/cpp`)

**What to test:**
- `DListFactory::parse()`: known GBI command sequence → `DListData` with correct `mGfxs` words
- Alignment = 8
- GBI minor version changes (SM64 vs MK64 vs PM64 vs None): correct command interpretation
- Code exporter: produces valid GBI macro calls for known commands
- Binary exporter: round-trip `uint32_t` GFX words

---

### 8. Texture Factory (`src/factories/TextureFactory.h/cpp`)

**What to test:**
- `TextureFactory::parse()`: binary data → `TextureData` with correct format, width, height, raw buffer
- Each `TextureType`: RGBA32, RGBA16, IA16, IA8, IA4, I8, I4, CI8, CI4, TLUT
- Modding support is enabled
- Modding exporter: produces valid PNG or expected modding format
- Code exporter: generates correct C array with format annotations
- Binary exporter: writes expected binary blob

---

### 9. Matrix Factory (`src/factories/MtxFactory.h/cpp`)

**What to test:**
- `MtxFactory::parse()`: 64-byte fixed-point N64 matrix → `MtxData` with correct float[16]
- Fixed-point ↔ float conversion accuracy
- `parse_modding()`: returns `nullopt` as expected (no modding support)
- Binary exporter: produces original 64-byte representation

---

### 10. Float/Blob/Lights/Viewport Factories

**What to test (per factory):**
- Known binary input → expected parsed data structure
- All declared exporters produce expected output
- Alignment values

---

### 11. SM64 Factories (`src/factories/sm64/`)

**GeoLayout** (`GeoLayoutFactory.h/cpp`):
- Parse known geo commands: 45+ opcodes (`BranchAndLink`, `End`, `OpenNode`, `CloseNode`, `NodeRoot`, `NodePerspective`, `NodeTranslation`, `NodeAnimation`, etc.)
- Hierarchical command nesting is preserved
- `skipped` flag handling
- Code and binary exporters

**BehaviorScript** (`BehaviorScriptFactory.h/cpp`):
- 70+ opcodes (BEGIN, DELAY, CALL, RETURN, GOTO, LOOP, etc.)
- Argument parsing: integers, floats, pointer variants
- Code exporter produces macro-style output

**Collision** (`CollisionFactory.h/cpp`):
- Vertex list, surface list, special objects, environment region boxes
- `CollisionVertex` (int16_t x/y/z), `CollisionTri` (3 indices + force)
- Terrain type and special preset encoding

**LevelScript** (`LevelScriptFactory.h/cpp`):
- Opcode + argument parsing across known level script sequence
- Command byte-length handling

**Animation** (`AnimationFactory.h/cpp`):
- Flags, frame count, loop info
- `mIndices` and `mEntries` arrays
- Binary-only exporter

---

### 12. SF64 Factories (`src/factories/sf64/`)

**SkeletonFactory:**
- `LimbData` hierarchy: address, display list, Vec3f translation, Vec3s rotation, sibling/child pointers
- Hierarchical limb tree is preserved

**MessageFactory:**
- Message codes → string representation
- Modding support and XML exporter
- Modding exporter produces expected human-readable format

---

### 13. MK64 Factories (`src/factories/mk64/`)

**PackedDisplayListFactory:**
- MK64 packed bytecode → expanded `DListData` with standard Gfx commands
- Zero alignment
- Reuses `DListData` exporters unchanged

---

### 14. F-Zero X Factories (`src/factories/fzerox/`)

**SequenceFactory:**
- `SeqCommand`: position, opcode, state, channel, layer, arguments
- `mLabels` set captures all label positions
- `mHasFooter` flag
- Code exporter generates expected sequence notation

---

### 15. NAudio v1 Factories (`src/factories/naudio/v1/`)

**AudioContext:**
- `MakeReader()` returns valid `BinaryReader` over audio table data
- `LoadTunedSample()` returns correct pitch/sample info
- `GetPathByAddr()` resolves known addresses

**SoundFontFactory:**
- Instrument/drum counts, bank IDs
- Instrument and drum pointer tables
- XML exporter output

**InstrumentFactory:**
- ADSR envelope parsing
- Tuned sample pitch ranges (low/normal/high)
- Modded dependency tracking

---

### 16. GenericArrayFactory / AssetArrayFactory

**GenericArrayFactory:**
- Variant type dispatch: u8, s8, u16, s16, u32, s32, float
- Array bounds and element count

**AssetArrayFactory:**
- Array of typed pointer entries
- Type string preservation

---

### 17. Companion Orchestration (`src/Companion.h/cpp`)

**What to test:**
- `RegisterFactory()` + `GetFactory()`: register a factory, retrieve it by type string
- `ParseNode()`: given a YAML node describing a known asset type, returns correct `ParseResultData`
- `GetParseDataByAddr()` / `GetParseDataBySymbol()`: lookup after parsing
- `GetFileOffsetFromSegmentedAddr()`: segment table lookup
- YAML configuration parsing: valid config → correct `TorchConfig` fields
- Error handling: invalid factory type → no crash, graceful `nullopt`
- Factory registry isolation: factories registered in one test don't bleed into another

**Note:** Companion uses a singleton-like pattern; tests need to handle reset/isolation carefully.

---

### 18. Archive Handling (`src/archive/`)

**Files:** `BinaryWrapper.h`, `ZWrapper.h/cpp`, `SWrapper.h/cpp`

**What to test:**
- `ZWrapper`: `CreateArchive()` → `AddFile()` → `Close()` produces a valid zip
- File can be added with known content and retrieved
- Thread safety: concurrent `AddFile()` calls don't corrupt
- `SWrapper` (MPQ, if StormLib is available): same create/add/close cycle

---

## Priority Ranking

| Priority | Category | Reason |
|---|---|---|
| P0 | BinaryReader/Writer | Foundation for all parsing |
| P0 | Decompressor | Required before any factory can parse |
| P1 | TextureUtils (CalculateTextureSize) | Used by many factories |
| P1 | Vec3D data types | Used pervasively |
| P1 | VtxFactory | Simple, well-bounded, high coverage value |
| P1 | DisplayListFactory | Core rendering asset |
| P2 | MtxFactory, FloatFactory, LightsFactory, ViewportFactory | Simple factories |
| P2 | SM64 GeoLayout, Collision, BehaviorScript | Complex but high value |
| P2 | Companion factory registration/lookup | Core dispatch mechanism |
| P3 | TextureFactory (format-specific) | Many code paths |
| P3 | SF64 Skeleton, Message | Game-specific |
| P3 | NAudio v1 context and fonts | Audio system |
| P3 | Archive wrappers | Integration-level |
| P4 | MK64 PackedDList, F-Zero Sequence, PM64 factories | Less critical initially |
| P4 | Companion full YAML parse + Process() | Integration test territory |

---

## Proposed Test Framework Setup

- **Framework:** Google Test (googletest) via CMake `FetchContent` — standard, well-supported, already used widely in similar projects
- **New CMake target:** `torch_tests` — separate executable, not linked to `main.cpp`
- **Test directory:** `tests/` (new), sibling to `src/` and `lib/`
- **Test fixtures:** small synthetic binary buffers constructed inline (no ROM files needed for unit tests)
- **Existing test data:** `test/` YAML fixtures available for integration-level tests

---

## Verification

After implementing the test framework and first tests:
1. `cmake -B build -DBUILD_TESTS=ON && cmake --build build`
2. `cd build && ctest --output-on-failure`
3. Confirm all P0 tests pass before moving to P1

---

## Critical Files

- `src/Companion.h` — core orchestrator interface
- `src/factories/BaseFactory.h` — factory + exporter interfaces
- `src/factories/VtxFactory.h/cpp`, `DisplayListFactory.h/cpp`, `TextureFactory.h/cpp`
- `src/factories/sm64/GeoLayoutFactory.h/cpp`, `CollisionFactory.h/cpp`, `BehaviorScriptFactory.h/cpp`
- `src/utils/Decompressor.h/cpp`, `TextureUtils.h/cpp`
- `src/types/Vec3D.h/cpp`, `RawBuffer.h`, `SegmentedAddr.h`
- `lib/binarytools/BinaryReader.h/cpp`, `BinaryWriter.h/cpp`
- `CMakeLists.txt` — needs test target added
