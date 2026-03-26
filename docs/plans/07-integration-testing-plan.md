# Integration Testing Implementation Plan

## Context

Torch has 323 unit tests covering data structures, factories, and utilities in isolation. What's missing is end-to-end validation: given a real ROM, does the full pipeline (ROM → Companion → factory parse → binary export) produce structurally valid assets? This plan adds integration tests that run the complete extraction pipeline against real ROM files and validate the output.

## Architecture

### Key Decisions
- **In-process gtest** — call Companion directly, not as external process
- **One YAML per test case** — each exercises a specific factory; adding tests = adding YAMLs
- **Minimal shared config.yml per ROM** — required by Companion::Process() architecture
- **Skip if ROM not present** — tests produce SKIPPED, not FAILED, when ROMs are absent
- **Structural validation** (test-side) — correct magic bytes, sizes, internal consistency
- **Error handling** — bad input doesn't crash, corruption is detected
- **Start with SM64 US** — SHA `9bef1128717f958171a4afac3ed78ee2bb4e86ce`

### Directory Structure
```
tests/
  roms/                                    # gitignored, user drops ROMs here
    README.md                              # supported ROMs and SHA-1 values
  integration/
    IntegrationTestHelpers.h               # ROM detection, header validation, fixture
    SM64IntegrationTest.cpp                # parametrized test code (small)
    sm64/
      us/
        config.yml                         # shared: SHA, GBI, segments, output path
        assets/
          test_texture_rgba16.yml          # one YAML per test case
          test_vtx_basic.yml
          test_blob_basic.yml
          test_collision.yml
          ...
```

### Why config.yml is needed
Companion::Process() (`src/Companion.cpp:1070`) loads `config.yml` from `gSourceDirectory`, matches the ROM SHA-1, reads GBI version + global segments, then walks YAML files under the `path:` directory. The global config and per-YAML asset configs are loaded by separate code paths and cannot be combined into one file without refactoring Companion.

The shared config.yml is small (~30 lines): just SHA, GBI: F3D, segments list, and output path. All test-case-specific data lives in individual YAMLs.

### Pipeline Flow
```
Companion(romPath, O2R, srcDir, destDir)
  → Init(ExportType::Binary)
    → RegisterFactory() for all types
    → Process()
      → Load config.yml, match ROM SHA
      → Set GBI, global segments, output path
      → Create ZWrapper (zip archive)
      → For each .yml in assets/:
        → ProcessFile(root)
          → Read :config: segments (local)
          → ParseNode() per asset entry
            → Factory::parse(romData, yamlNode) → IParsedData
            → BinaryExporter::Export(stream, data) → binary bytes
            → ZWrapper::AddFile(name, bytes)
      → ZWrapper::Close() → test_output.o2r
```

Test reads back the .o2r (zip) and validates each asset's binary content.

### Binary Asset Format
Every exported asset has a 0x40-byte header (`BaseExporter::WriteHeader` in `src/factories/BaseFactory.cpp`):

| Offset | Size | Content |
|--------|------|---------|
| 0x00 | 1 | Endianness (Native=0) |
| 0x01-0x03 | 3 | Reserved (0) |
| 0x04 | 4 | Resource type magic (uint32_t) |
| 0x08 | 4 | Version (0) |
| 0x0C | 8 | 0xDEADBEEFDEADBEEF marker |
| 0x14-0x3F | 44 | Reserved/padding (0) |

Resource type magics defined in `src/factories/ResourceType.h`.

---

## CMake Changes

Add within the existing `if(BUILD_TESTS)` block in `CMakeLists.txt`:

- New option: `BUILD_INTEGRATION_TESTS` (OFF by default, separate from unit tests)
- Glob integration test sources from `tests/integration/*.cpp`
- New executable `torch_integration_tests` linking same libs as `torch_tests`
- Compile definitions: `INTEGRATION_TEST_DIR` and `INTEGRATION_ROM_DIR` pointing to source tree paths
- Include `tests/integration/` for IntegrationTestHelpers.h

---

## Test Infrastructure

### IntegrationTestHelpers.h

**ROM detection + skip:**
- `RomExists(path)` — checks filesystem
- SHA-1 constants per supported ROM
- Tests call `GTEST_SKIP()` when ROM absent

**Singleton lifecycle fixture:**
- `SetUp()`: delete old `Companion::Instance`, `AudioManager::Instance`; call `Decompressor::ClearCache()`
- `TearDown()`: same cleanup
- AudioManager is created by `Process()` at `src/Companion.cpp:1312` — must be cleaned up between runs

**Binary header validation:**
- `ParseHeader(data)` → struct with endianness, resourceType, version, deadbeef
- `ValidateHeader(data, expectedResourceType)` — asserts size >= 0x40, correct magic, version=0, DEADBEEF present

**Pipeline runner:**
- `RunPipeline(configDir, romPath)` → constructs Companion, calls Init(Binary), reads back .o2r zip
- Returns `map<string, vector<char>>` of asset name → binary content
- Uses miniz-cpp (`<miniz/zip_file.hpp>`, already a dependency) to read the zip
- Writes to temp directory, cleans up after

### Test Execution Model
Process() walks all YAMLs in the assets directory in one run, producing one .o2r. The test runs the pipeline once in a static initializer, caches all extracted assets, then individual TEST_F cases validate specific assets. This avoids re-running the pipeline per test.

---

## SM64 US Config

**tests/integration/sm64/us/config.yml:**
```yaml
9bef1128717f958171a4afac3ed78ee2bb4e86ce:
  name: Super Mario 64 [US]
  path: assets
  config:
    gbi: F3D
    sort: OFFSET
    output:
      binary: test_output.o2r
  segments:
    - 0x000000
    - 0x000000
    - 0x108a40
    - 0x201410
    - 0x114750
    - 0x12a7e0
    - 0x188440
    - 0x26a3a0
    - 0x1f2200
    - 0x31e1d0
    - 0x2708c0
    - 0x36f530
    - 0x132850
    - 0x1b9070
    - 0x3828c0
    - 0x2008d0
    - 0x108a10
    - 0x000000
    - 0x000000
    - 0x219e00
    - 0x269ea0
    - 0x2abca0
    - 0x218da0
    - 0x1279b0
```

---

## Initial Test Cases

### Commit 2: TEXTURE (RGBA16)
**test_texture_rgba16.yml** — Amp body texture (32x32 RGBA16, 2048 bytes)
```yaml
:config:
  segments:
    - [ 0x8, 0x1F2200 ]
  force: true

test_texture:
  type: TEXTURE
  size: 2048
  width: 32
  height: 32
  offset: 0x1B18
  format: RGBA16
  symbol: test_amp_body_texture
```
**Validation:** header magic = OTEX (0x4F544558), format_type valid, width=32, height=32, buffer_size=2048, total size = 0x40 + 16 + 2048

### Commit 3: VTX
**test_vtx_basic.yml** — Amp body vertices (4 vertices)
```yaml
:config:
  segments:
    - [ 0x8, 0x1F2200 ]
  force: true

test_vtx:
  type: VTX
  count: 4
  offset: 0x2DE0
  symbol: test_amp_body_vtx
```
**Validation:** header magic = OVTX (0x4F565458), count=4, total size = 0x40 + 4 + 4*16

### Commit 4: BLOB
**test_blob_basic.yml** — Raw bytes from ROM header area
```yaml
:config:
  force: true

test_blob:
  type: BLOB
  size: 64
  offset: 0x40
  symbol: test_blob_data
```
**Validation:** header magic = OBLB (0x4F424C42), size=64, total size = 0x40 + 4 + 64

### Commit 5: SM64:COLLISION
**test_collision.yml** — Collision data from a known level
```yaml
:config:
  segments:
    - [ 0x7, 0x26a3a0 ]
  force: true

test_collision:
  type: SM64:COLLISION
  offset: 0x6580
  symbol: test_collision_data
```
**Validation:** header magic = COL (0x434F4C20), command count > 0, total size = 0x40 + 4 + count*2

**Note:** Exact offsets for VTX and COLLISION need verification against the actual ROM during implementation. The texture offset is from Ghostship's amp.yml and is known-good.

---

## Structural Validation Per Type

| Type | Magic | Payload checks |
|------|-------|----------------|
| Vertex (OVTX) | 0x4F565458 | count > 0, size == 0x40 + 4 + count*16 |
| Texture (OTEX) | 0x4F544558 | format valid, w/h > 0, buf_size matches w*h*bpp/8, size == 0x40 + 16 + buf_size |
| Blob (OBLB) | 0x4F424C42 | size field > 0, total == 0x40 + 4 + size |
| Collision (COL) | 0x434F4C20 | cmd_count > 0, total == 0x40 + 4 + count*2 |
| DisplayList (ODLT) | 0x4F444C54 | GBI version byte, 8-byte aligned, commands in pairs |
| Animation (ANIM) | 0x414E494D | flags, frame counts, index/entry arrays present |

All types share: size >= 0x40, correct magic at 0x04, version=0 at 0x08, DEADBEEF at 0x0C.

---

## Error/Corruption Tests

Separate pipeline runs (not mixed with happy-path YAMLs):

- **Bad offset** — YAML points beyond ROM bounds → pipeline doesn't crash
- **Invalid texture format** — `format: INVALID_FORMAT` → graceful error
- **Missing required fields** — VTX without `count:` → graceful error/skip

Tests use `EXPECT_NO_FATAL_FAILURE` wrapping try/catch — we verify no crash/SEGV, not specific error messages.

---

## Implementation Order

### Commit 1: Infrastructure
- `tests/roms/README.md` + `.gitignore` entries
- `tests/integration/IntegrationTestHelpers.h` — ROM detection, header validation, fixture, pipeline runner
- `tests/integration/SM64IntegrationTest.cpp` — fixture class, skip logic, smoke test (just verifies ROM detection works)
- `CMakeLists.txt` — `BUILD_INTEGRATION_TESTS` option, new target
- Verify: compiles, skips gracefully without ROM

### Commit 2: First test case (TEXTURE)
- `tests/integration/sm64/us/config.yml`
- `tests/integration/sm64/us/assets/test_texture_rgba16.yml`
- Full pipeline + texture structural validation
- This is the "prove it works" commit

### Commit 3: VTX test case
- `tests/integration/sm64/us/assets/test_vtx_basic.yml`
- Vertex count + size validation

### Commit 4: BLOB test case
- `tests/integration/sm64/us/assets/test_blob_basic.yml`
- Size validation (simplest factory)

### Commit 5: SM64:COLLISION test case
- `tests/integration/sm64/us/assets/test_collision.yml`
- Command-based parsing validation

### Commit 6: Error handling tests
- Error YAML files + error test methods
- Verify no crash on bad input

### Future commits: Additional factories
- SM64:ANIM, SM64:GEO_LAYOUT, SM64:BEHAVIOR_SCRIPT, GFX, LIGHTS, etc.
- Each is just a new YAML + a new TEST_F with type-specific validation

---

## Singleton/Cleanup Concerns

- `Companion::Instance` — raw pointer singleton, must `delete` + null between runs
- `AudioManager::Instance` — created by `Process()`, same cleanup needed
- `Decompressor::ClearCache()` — static cache of decompressed data chunks
- Tests must run sequentially (GTest default)

---

## Critical Files

| File | Role |
|------|------|
| `CMakeLists.txt` | Add BUILD_INTEGRATION_TESTS, new executable |
| `tests/integration/IntegrationTestHelpers.h` | New — shared test utilities |
| `tests/integration/SM64IntegrationTest.cpp` | New — test code |
| `tests/integration/sm64/us/config.yml` | New — SM64 US ROM config |
| `tests/integration/sm64/us/assets/*.yml` | New — one per test case |
| `tests/roms/README.md` | New — ROM instructions |
| `src/Companion.cpp` | Read-only — pipeline code being tested |
| `src/factories/BaseFactory.cpp` | Read-only — WriteHeader format |
| `src/factories/ResourceType.h` | Read-only — magic byte constants |
| `src/archive/ZWrapper.cpp` | Read-only — zip output format |

## Verification

```bash
# Without ROM (should skip):
cmake -B build -DBUILD_TESTS=ON -DBUILD_INTEGRATION_TESTS=ON && cmake --build build && build/torch_integration_tests

# With ROM (copy SM64 US .z64 to tests/roms/):
cp /path/to/sm64.z64 tests/roms/sm64.us.z64
cmake -B build -DBUILD_TESTS=ON -DBUILD_INTEGRATION_TESTS=ON && cmake --build build && build/torch_integration_tests
```
