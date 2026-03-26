# Test Review: unit-testing-discovery branch

## Scope

This document reviews every test added in the `unit-testing-discovery` branch: 31 unit test files (~400 test cases) and 15 integration tests across 16 YAML configs. The review assesses whether each test makes sense, flags issues or gaps, and notes patterns worth changing.

Tests are organized by phase (P0–P4) as planned. Source code was not modified; all tests are non-invasive.

---

## Summary Table

| Phase | Files | Verdict | Notes |
|-------|-------|---------|-------|
| P0 — Foundation | BinaryReader, BinaryWriter, StringHelper, TorchUtils, Decompressor, Smoke | ✅ Solid | One duplicate in Decompressor |
| P1 — Data Types | Vec3D, TextureUtils, VtxFactory, DisplayListFactory | ⚠️ Mostly good | Suspicious texture size; low-ROI struct tests |
| P2 — Simple Factories | Viewport, Lights, Float, Mtx, SM64Collision, SM64GeoLayout, SM64BehaviorScript, CommandMacros, CompanionRegistration | ⚠️ Mixed | High-value: CommandMacros, SM64Collision; low-value: Viewport/Lights/Float/GeoLayout/BehaviorScript |
| P3 — Utilities | StrHash64, N64Graphics, NAudioContext, NAudioData, SF64Data, TextureFactory | ✅ Strong | N64Graphics is the best file in the branch |
| P4 — Game-Specific | MK64Data, FZXGhostRecord, FZXCourse, FZXData, PM64Data, SM64Animation | ⚠️ Mixed | High-value: FZXGhostRecord/Course; low-value: MK64/PM64/FZXData struct tests |
| Integration | Infrastructure, SM64 US (16 assets), error handling | ✅ Well designed | CI skip semantics and fuzzy asset lookup are worth addressing |

---

## P0 — Foundation

### BinaryReaderTest.cpp ✅

Tests all numeric types (int8–int64, uint variants, float, double), endianness, seek, CString, and length-prefixed strings. All use inline byte buffers; no Companion dependency. The `ReadDouble` test relies on IEEE 754 representation — correct on all target platforms.

### BinaryWriterTest.cpp ✅

Write operations and round-trip tests (BinaryWriter → BinaryReader) confirm writer and reader agree on byte layout for all supported types.

### StringHelperTest.cpp ✅

Covers all public methods: `Split`, `Strip`, `Replace`, `StartsWith`, `EndsWith`, `Contains`, `IEquals`, `HasOnlyDigits`, `IsValidHex`, `IsValidOffset`, `StrToL`, `BoolStr`. Positive, negative, and edge cases are all present.

### TorchUtilsTest.cpp ✅

Concise. Tests `to_hex<uint8_t/uint16_t/uint32_t>` with and without `0x` prefix, and `contains` for map/set lookups.

### DecompressorTest.cpp ⚠️

Compression type detection is well covered: MIO0, YAY0, PERS (alias for YAY0), YAY1, YAZ0, and unknown magic. The `IS_SEGMENTED`, `SEGMENT_NUMBER`, `SEGMENT_OFFSET`, and `ASSET_PTR` macro tests are correct — pure bit logic, no Companion needed.

**Issue:** `GetCompressionTypeMIO0` (line 8) and `GetCompressionTypeZeroOffset` (line 46) both assert that `offset=0` returns `None`. One is redundant.

`ClearCacheNoCrash` is a minimal smoke test but acceptable.

### SmokeTest.cpp ✅

Framework sanity check. Fine.

---

## P1 — Data Types

### Vec3DTest.cpp ✅

Covers Vec3f, Vec3s, Vec3i, Vec3iu, Vec2f, Vec4f: construction, field access, default construction, stream output formatting, width/precision calculation. The comment noting that `Vec2f`'s second field is named `.z` (not `.y`) documents a genuine gotcha in the type definition.

### TextureUtilsTest.cpp ⚠️

All 12 texture formats tested at 32×32, plus non-square and 1×1 edge cases. Good systematic coverage.

**Issue — suspicious value:** `GrayscaleAlpha1bpp` returns `1024u` for 32×32. At 1 bpp, 32×32 = 1024 bits = 128 bytes. The test produces the same result as 8bpp formats (`GrayscaleAlpha8bpp` also returns 1024u). Either `GrayscaleAlpha1bpp` does not mean "1 bit per pixel" (the enum name is misleading), or `CalculateTextureSize` has a bug for this type and the test is locking it in. This is worth investigating before merging — the test may be documenting a production defect rather than correct behavior.

**Gap:** `alloc_ia8_text_from_i1` tests use only `0xFFFF` (all ones) and `0x0000` (all zeros). Both are byte-palindromes, so BSWAP16 produces the same value — the tests are endianness-neutral by accident. A test with `0x8000` or `0x0100` would catch platform-specific behavior. Not blocking, but a known gap.

### VtxFactoryTest.cpp ⚠️

Tests that a vertex struct stores what you put in (position, uv, color, normal, flag fields). No parse logic is exercised. Useful as a struct layout regression guard (catches field reorders/renames during refactors) but catches no behavioral bugs.

### DisplayListFactoryTest.cpp ⚠️

Same pattern as VtxFactory. The GBI opcode extraction test (`GetOpcode` from the high byte of a word) is slightly more meaningful than the field-access tests.

---

## P2 — Simple Factories

### ViewportFactoryTest.cpp, LightsFactoryTest.cpp, FloatFactoryTest.cpp ⚠️

All three follow the same pattern: construct a struct, verify fields store what was passed in. No logic exercised. These serve as refactor regression guards but have weak behavioral value. Acceptable — not harmful, just low yield.

### MtxFactoryTest.cpp ✅

Better than the others. Tests fixed-point math in the matrix constructor: `0x00010000` represents 1.0 in 16.16 fixed-point, and the test verifies the conversion. This is genuinely useful.

### SM64CollisionFactoryTest.cpp ✅

Replicates factory parse logic manually using `BinaryReader` with crafted byte sequences. Tests surface type commands (`TYPE_SURFACE_DEFAULT`, `SURFACE_0005`), vertex count parsing, vertex data, and triangle data. This is the right approach for testing parse behavior without Companion — it catches real parsing bugs.

### SM64GeoLayoutTest.cpp, SM64BehaviorScriptTest.cpp ⚠️

Struct construction for individual GeoLayout/BehaviorScript commands. Same low-value pattern as the simple factory tests — these don't test any interpretation of the command bytes, just storage.

### CommandMacrosTest.cpp ✅

Tests `_SHIFTL`, `_SHIFTR`, `CMD_BBH`, `CMD_HH`, `CMD_W`, and `CMD_PROCESS_OFFSET` with exact expected values. These macros underpin every GBI display list command in the codebase. Verifying their bit behavior is high value.

`CMD_SIZE_SHIFT` is tested as a constant (`== 0u`). This would only be wrong on a platform with `sizeof(uint32_t) != 4`, which is not a realistic target. Borderline, but harmless.

### CompanionFactoryRegistrationTest.cpp ✅

Tests `RegisterFactory`/`GetFactory`: happy path, missing key returns `nullopt`, multiple coexisting factories, overwrite semantics. Also verifies `GetExporter` and `GetAlignment` through a registered factory — this is fine-grained but appropriate.

Good design: constructs `Companion` with an empty vector and `ArchiveType::None` to avoid any ROM dependency.

---

## P3 — Utilities & Format Libraries

### StrHash64Test.cpp ✅

Tests determinism (same string → same hash), distinctness for different inputs, case sensitivity (upper ≠ lower), and incremental hash updates. All meaningful properties for a hash function used in asset lookup.

### N64GraphicsTest.cpp ✅

The strongest test file in the branch. Tests RGBA16, RGBA32, IA16, IA8, I8, I4, CI8, CI4 with both decode (`raw2*`) and encode (`*2raw`) paths, plus round-trips for each. Known pixel values verify the scaling math (SCALE_5_8, SCALE_4_8) implicitly. Round-trip tests catch encode/decode asymmetry. Well-structured throughout.

### NAudioContextTest.cpp ✅

Tests `MediumStr`, `CachePolicyStr`, `CodecStr` for all known enum values plus `Unknown`. These are lookup tables, so mechanical tests are appropriate — any typo or missed case would be caught.

### NAudioDataTest.cpp ⚠️

`AudioTableEntry` and `EnvelopePoint` struct construction. Low behavioral coverage — confirms the compiler didn't mangle the struct, nothing more.

### SF64DataTest.cpp ⚠️

`LimbData` and `SkeletonData` struct construction. `MessageData` vector storage. Same low-value pattern.

### TextureFactoryTest.cpp ✅

Tests `TextureFormat` string-to-type conversion and `GetDepth()` for each format. Depth values flow into size calculations throughout the codebase, so testing them here matters.

---

## P4 — Game-Specific

### MK64DataTest.cpp ⚠️

`CourseVtxData`, `TrackPathData`, `TrackSectionsData`, `ActorSpawnData`, `DrivingBehaviourData`, `ItemCurveData` — all struct construction. None tests parse logic. Fine as regression guards; no behavioral coverage.

### FZXGhostRecordTest.cpp ✅

`Save_CalculateChecksum`: tested with a 4-byte buffer (`0x10+0x20+0x30+0x40 = 0xA0`), empty buffer, and single byte. High confidence in the simple sum algorithm.

`CalculateReplayChecksum`: tests 4-byte aligned, 8-byte aligned, and 5-byte non-aligned (partial word at end is ignored). The non-aligned case is the right edge case to have for this algorithm.

Factory exporter test checks that Binary, Code, Header, and Modding are all present, and that `SupportModdedAssets()` returns true. Appropriate.

### FZXCourseTest.cpp ✅

`ControlPoint` and `CourseData` construction. Track mask constant verification (`TRACK_MASK_MUTE_CITY`, `TRACK_MASK_PORT_TOWN`). Checksum determinism test: same input → same checksum, different input → different checksum. This property-based approach is the right idiom here.

### FZXDataTest.cpp ⚠️

`EADAnimationData`, `EADLimbData`, `DrumData`, `InstrumentData`, `SeqCommandData`, `SequenceData` — struct construction throughout. `AdpcmBookData` order/npredictors field test is slightly more meaningful (these control codec behavior). Overall low behavioral coverage.

### PM64DataTest.cpp ⚠️

`ShapeData` and `EntityGfxData` struct construction. No logic exercised.

### SM64AnimationTest.cpp ✅

`AnimationDataConstruction` verifies all 7 fields plus index and entry vector contents.

`AnimindexCountMacro` tests the `ANIMINDEX_COUNT(boneCount) = (boneCount + 1) * 6` formula at several values. **Minor issue:** The macro is redefined locally in the test file (`#define ANIMINDEX_COUNT`) rather than included from the production header. If the production formula changes, this test won't catch it unless both definitions are updated. The test should include the production header instead.

Factory exporter test explicitly verifies Binary is present and Code/Header/Modding are absent. Testing negative cases is valuable.

---

## Integration Tests

### Infrastructure (IntegrationTestHelpers.h/cpp) ✅

`RunPipeline()` invokes Companion in-process (not as a subprocess) — better for debuggability and failure attribution. Singleton cleanup (`Companion::Instance`, `AudioManager::Instance`, `Decompressor::ClearCache()`) is handled between runs. `ValidateHeader()` checks endianness byte, resource type magic, version=0, and DEADBEEF marker — implicitly exercises `BaseFactory::WriteHeader`.

Test suite uses a static setup (`SetUpTestSuite`) to run the pipeline once and cache all assets, then individual tests validate specific entries. This avoids re-running the expensive Companion pipeline per test.

**Minor:** `SM64_US_SHA1` is defined in the header but never used — Companion validates the SHA internally. It's documentation-only; fine, just worth noting.

### SM64USIntegrationTest.cpp

| Test | Verdict | Notes |
|------|---------|-------|
| PipelineProducesAssets | ✅ | Smoke test: pipeline ran and produced something |
| TextureRGBA16 | ✅ | width=32, height=32, bufSize=2048, total size validated |
| VtxBasic | ✅ | count=4, total = 0x40 + 4 + count×16 |
| BlobBasic | ✅ | size=64, total = 0x40 + 4 + 64 |
| CollisionBasic | ✅ | cmdCount > 0, total = 0x40 + 4 + count×2 |
| GfxBasic | ✅ | GBI version byte ≥ 0, BEEFBEEF marker at expected offset |
| LightsBasic | ✅ | Fixed 24-byte Lights1Raw payload |
| AnimBasic | ✅ | indicesCount > 0 at offset 0x54 |
| DialogBasic | ✅ | textSize > 0 at offset 0x49; memcpy handles unaligned read safely |
| TextBasic | ✅ | SM64:TEXT exports as Blob type — documented non-obvious behavior |
| MacroBasic | ✅ | count > 0, total = 0x40 + 4 + count×2 |
| MovtexBasic | ✅ | bufSize > 0, total = 0x40 + 4 + bufSize×2 |
| MovtexQuadBasic | ⚠️ | count=2 hardcoded; must stay in sync with test_movtex_quad.yml |
| TrajectoryBasic | ✅ | count > 0, total = 0x40 + 4 + count×8 |
| PaintingBasic | ⚠️ | Only checks `> 0x40 + 20` — weakest validation in the suite |
| PaintingMapBasic | ✅ | totalElements > 0 |
| DictionaryBasic | ⚠️ | dictSize=3 hardcoded; must stay in sync with test_dictionary.yml |

### Error handling test ✅

`EXPECT_NO_FATAL_FAILURE` wrapping try/catch is the correct idiom for "don't crash or SEGV". Does not validate error messages or that errors are reported to the user — acceptable for a first pass.

---

## Cross-Cutting Concerns

### `GetAsset()` fuzzy matching — fragility risk

`key.find(name) != npos` returns the first asset whose path contains the search string. If two assets share a name prefix — e.g., `test_movtex` and `test_movtex_quad` — searching for `"test_movtex"` could return the wrong one depending on `std::map` iteration order (which is alphabetical). Current asset names side-step this (`"test_movtex"` sorts before `"test_movtex_quad"` alphabetically, so the search works today), but it's a latent bug. Consider requiring exact suffix matches or unique token boundaries.

### Binary offset hardcoding in integration tests — intentional

Tests hard-code offsets like `data.data() + 0x54` (animation indices count) and `data.data() + 0x49` (dialog text size). These document the binary format contract explicitly. If the format changes, the tests break — which is the intended behavior. No change needed; just be aware these are format-documenting tests, not structural property tests.

### CI integration test skip semantics — design trade-off

In GitHub Actions (no ROMs present), `build/torch_integration_tests` exits 0 with all tests reporting `SKIPPED`. CI green status guarantees: compilation succeeds and unit tests pass. It does not guarantee the pipeline ever ran. This is an acceptable trade-off — ROMs cannot be committed to the repo. The CI workflow should note this explicitly (e.g., a comment in `tests.yml`) so future contributors understand why integration tests appear to always skip in CI.

### `ANIMINDEX_COUNT` redefined locally

`SM64AnimationTest.cpp` defines `#define ANIMINDEX_COUNT(boneCount) (((boneCount) + 1) * 6)` rather than including the production header. If the production formula changes, both the production code and the test definition must be updated independently — the test won't catch the drift. Prefer including the header.

---

## Coverage Gaps (Acknowledged)

These are not defects in the tests — they reflect deliberate scope decisions from the planning docs:

- **Factory `parse()` methods:** Cannot be unit-tested without a live Companion and ROM. Integration tests cover these.
- **Non-SM64 games in integration tests:** 79 factory types total; ~16 tested end-to-end. MK64, SF64, PM64, FZX have unit tests for data structures but no integration coverage.
- **Non-Binary export paths:** Code, Header, XML, and Modding exporters are not tested. Only Binary export is exercised in integration.
- **YAML parsing correctness:** Tested only implicitly (a bad YAML would cause the pipeline to skip or fail the integration test).
- **Archive operations (`SWrapper`):** Filesystem I/O; requires an integration approach. Not tested.

---

## Overall Assessment

The testing infrastructure is well-designed. The phased approach (P0 foundation → P4 game-specific), the decision to avoid Companion coupling in unit tests, and the single-pipeline-run integration design all reflect sound engineering judgment.

The clearest high-value tests are: `N64GraphicsTest`, `CommandMacrosTest`, `FZXGhostRecordTest`, `SM64CollisionFactoryTest`, `BinaryReader/Writer`, and the integration test suite. These test real behavior with known inputs and expected outputs.

The recurrent weak pattern is struct construction tests that only verify fields store what you put in. Roughly 8–10 test files fall into this category. They're not wrong — they serve as refactor regression guards — but they have low expected defect-detection value. Future test additions for these factories should aim to test parse logic with synthetic `BinaryReader` buffers (following the `SM64CollisionFactoryTest` pattern) rather than just constructors.

The two items most worth acting on before merging:

1. **`GrayscaleAlpha1bpp` returning 1024 in TextureUtilsTest** — verify whether the production function is correct for this type, or if the test is locking in a bug.
2. **`GetAsset()` fuzzy matching** — make asset lookup exact to prevent future test fragility as more assets are added.
