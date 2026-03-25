# Implementation Plan: Unit Test Framework & P0 Tests

## Context

The testing discovery document (`docs/unit-testing-discovery.md`) identifies all testing opportunities in the Torch codebase. No C++ test framework exists yet. This plan covers: (1) setting up Google Test, (2) implementing P0 tests (BinaryReader/Writer, Decompressor, StringHelper), then (3) P1 tests.

The user wants small, incremental commits — not one commit per file, but not one giant commit either. A few tests per commit is the right granularity.

---

## Commit 1: Google Test framework setup with smoke test

**Files to create/modify:**

- **`CMakeLists.txt`** — Add at the end (after line 291):
  - `option(BUILD_TESTS "Build unit tests" OFF)`
  - Guard the test block with `if(BUILD_TESTS)`
  - FetchContent googletest (follow existing pattern: `GIT_REPOSITORY`, `GIT_TAG`, set `BUILD_GMOCK OFF`, `INSTALL_GTEST OFF`)
  - `enable_testing()`
  - Glob test sources from `tests/*.cpp`
  - Create `torch_tests` executable linking: test sources + all project sources except `main.cpp` (filter via `list(FILTER ... EXCLUDE REGEX "main.cpp")`)
  - Link libraries: `gtest_main`, `yaml-cpp`, `tinyxml2`, `N64Graphics`, `BinaryTools`, `spdlog`
  - Same include directories as main target
  - `add_test(NAME torch_tests COMMAND torch_tests)`

- **`tests/SmokeTest.cpp`** (new) — Minimal test:
  ```cpp
  #include <gtest/gtest.h>
  TEST(SmokeTest, TrueIsTrue) { EXPECT_TRUE(true); }
  TEST(SmokeTest, FalseIsFalse) { EXPECT_FALSE(false); }
  ```

**Verify:** `cmake -B build -DBUILD_TESTS=ON && cmake --build build && cd build && ctest --output-on-failure`

---

## Commit 2: BinaryReader basic type reads (P0)

**File:** `tests/BinaryReaderTest.cpp`

Tests using known byte buffers constructed inline via `std::vector<char>`:
- Read `int8`, `uint8` from 1-byte buffer
- Read `int16`, `uint16` from 2-byte big-endian buffer
- Read `int32`, `uint32` from 4-byte buffer
- Read `float` from 4-byte buffer
- Read `uint64` from 8-byte buffer

Construction pattern: `BinaryReader reader(buf.data(), buf.size()); reader.SetEndianness(Torch::Endianness::Big);`

**Source files:**
- `lib/binarytools/BinaryReader.h` — public API
- `lib/binarytools/endianness.h` — `Torch::Endianness` enum

---

## Commit 3: BinaryReader strings, seek, endianness (P0)

**File:** `tests/BinaryReaderTest.cpp` (append)

- `ReadCString`: buffer with null-terminated string
- `ReadString`: buffer with length-prefixed string
- Seek: read at offset 0, seek forward, read again, verify values
- Endianness: same 4-byte buffer read as big-endian vs little-endian produces different `uint32` values
- `GetLength`, `GetBaseAddress` after seek

---

## Commit 4: BinaryWriter + round-trip tests (P0)

**File:** `tests/BinaryWriterTest.cpp`

- Write `int8`, `int16`, `int32`, `uint32`, `float`, `double` → `ToVector()` → verify byte sequences
- Endianness: write uint32 in big-endian, verify byte order
- Round-trip: BinaryWriter writes values → BinaryReader reads them back → values match
- String write: write string with length prefix, verify bytes

Construction pattern: `BinaryWriter writer; writer.SetEndianness(Torch::Endianness::Big);`

---

## Commit 5: StringHelper tests (P0)

**File:** `tests/StringHelperTest.cpp`

- `Split`: "a,b,c" by "," → {"a","b","c"}; empty string; no delimiter found
- `Strip`: remove substring
- `Replace`: substitute substring
- `StartsWith`, `EndsWith`, `Contains`: positive and negative cases
- `IEquals`: case-insensitive comparison
- `HasOnlyDigits`: "123" → true, "12a" → false, "" → edge case
- `IsValidHex`: "0xABCD" → true, "GHIJ" → false
- `IsValidOffset`: valid/invalid offset strings
- `StrToL`: decimal and hex parsing
- `BoolStr`: true → "true", false → "false"

**Source file:** `src/utils/StringHelper.h`

---

## Commit 6: TorchUtils tests (P0)

**File:** `tests/TorchUtilsTest.cpp`

- `to_hex<uint32_t>`: known values → expected hex strings, with and without 0x prefix
- `to_hex<uint8_t>`, `to_hex<uint16_t>`: size variants
- `contains`: map/set with and without key

**Source file:** `src/utils/TorchUtils.h`

---

## Commit 7: Decompressor utility tests (P0)

**File:** `tests/DecompressorTest.cpp`

- `GetCompressionType`: construct buffers with MIO0/YAY0/YAZ0 magic bytes at offset → verify correct enum returned; buffer without magic → `None`
- `IsSegmented`: addresses with high nibble set (e.g. `0x06001000`) → true; physical addresses → false
- `TranslateAddr`: requires Companion segment table setup — may need to test in isolation or skip if tightly coupled
- `ClearCache`: call after Decode, verify no crash (functional test)

**Source files:**
- `src/utils/Decompressor.h` — `CompressionType` enum, static methods

---

## Commit 8+: P1 tests (Vec3D, TextureUtils, VtxFactory, DisplayListFactory, CompTool)

After P0 is solid, continue with P1 from the discovery doc. Same small-commit pattern:
- `tests/Vec3DTest.cpp` — construction, field access, stream output for Vec3f/Vec3s/Vec3i
- `tests/TextureUtilsTest.cpp` — `CalculateTextureSize` for each TextureType
- Factory tests will need more exploration of how `parse()` is called (what YAML node shape, what buffer layout) — plan those in detail when we get there

---

## Critical Files

| File | Role |
|---|---|
| `CMakeLists.txt` | Add BUILD_TESTS option, googletest fetch, test target |
| `tests/SmokeTest.cpp` | New — framework verification |
| `tests/BinaryReaderTest.cpp` | New — P0 BinaryReader tests |
| `tests/BinaryWriterTest.cpp` | New — P0 BinaryWriter tests |
| `tests/StringHelperTest.cpp` | New — P0 StringHelper tests |
| `tests/TorchUtilsTest.cpp` | New — P0 TorchUtils tests |
| `tests/DecompressorTest.cpp` | New — P0 Decompressor tests |
| `lib/binarytools/BinaryReader.h` | Read-only reference |
| `lib/binarytools/BinaryWriter.h` | Read-only reference |
| `src/utils/StringHelper.h` | Read-only reference |
| `src/utils/TorchUtils.h` | Read-only reference |
| `src/utils/Decompressor.h` | Read-only reference |

## Verification

After each commit:
```bash
cmake -B build -DBUILD_TESTS=ON && cmake --build build && cd build && ctest --output-on-failure
```
