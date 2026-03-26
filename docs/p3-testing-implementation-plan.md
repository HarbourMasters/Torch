# P3 Testing Implementation Plan

## Context

P0 (88 tests), P1 (48 tests), and P2 (59 tests) are complete, totaling 195 tests across 19 test suites. This plan covers P3: TextureFactory data structures, SF64 Skeleton/Message, NAudio v1 data structures and utility methods, n64graphics pixel format conversions, and strhash64 CRC64 hashing.

Key findings:
- TextureFactory's `sTextureFormats` map is file-local (`static`) and not directly testable. TextureData construction and the TextureFormat struct are testable.
- SF64 SkeletonFactory's LimbData/SkeletonData and MessageFactory's MessageData are testable data structures. The `getCharByCode` function is file-local in MessageFactory.cpp, not externally accessible.
- NAudio v1 AudioContext has three standalone string conversion methods (`GetMediumStr`, `GetCachePolicyStr`, `GetCodecStr`) that are pure switch statements with no dependencies.
- NAudio v1 data structures (AudioTableEntry, EnvelopePoint, etc.) are simple structs testable via construction.
- n64graphics is a C library with `raw2rgba`, `raw2ia`, `raw2i`, `rgba2raw`, `ia2raw`, `i2raw` conversion functions that operate on raw byte buffers — fully testable with known pixel data. Scaling macros (`SCALE_5_8`, etc.) are file-local in the .c file.
- strhash64 exposes `CRC64()`, `crc64()`, and `update_crc64()` — pure functions with no dependencies.
- Archive wrappers (SWrapper) require StormLib and filesystem operations — skip for unit tests.

---

## Commit 1: strhash64 CRC64 tests

**File:** `tests/StrHash64Test.cpp`

Pure hash functions, no dependencies.

- `CRC64("")`: hash of empty string
- `CRC64("hello")`: hash of known string, verify deterministic
- `CRC64("hello") != CRC64("Hello")`: case-sensitive
- `CRC64` same string twice returns same value
- `crc64(buf, len)`: buffer-based hash matches CRC64 for same content
- `update_crc64`: incremental hash produces same result as single-pass

---

## Commit 2: n64graphics RGBA conversion tests

**File:** `tests/N64GraphicsTest.cpp`

Test raw N64 texture format conversions with small known pixel buffers.

- `raw2rgba` with RGBA16 (depth=16): 2-byte pixel → verify R/G/B/A components after 5-bit-to-8-bit scaling
- `raw2rgba` with RGBA32 (depth=32): 4-byte pixel → verify exact R/G/B/A passthrough
- `rgba2raw` with RGBA16: known RGBA → verify 2-byte big-endian encoding
- `rgba2raw` with RGBA32: known RGBA → verify 4-byte passthrough
- Round-trip: rgba2raw → raw2rgba → verify values match (with scaling precision loss for RGBA16)

---

## Commit 3: n64graphics IA/I conversion tests

**File:** `tests/N64GraphicsTest.cpp` (append)

- `raw2ia` with IA8 (depth=8): 1-byte pixel (high nibble=intensity, low nibble=alpha) → verify 4-to-8-bit scaling
- `raw2ia` with IA16 (depth=16): 2-byte pixel (intensity, alpha) → verify exact passthrough
- `raw2i` with I8 (depth=8): 1-byte pixel → verify intensity=value, alpha=0xFF
- `raw2i` with I4 (depth=4): packed nibbles → verify 4-to-8-bit scaling
- `ia2raw` round-trip with IA16
- `i2raw` round-trip with I8

---

## Commit 4: n64graphics CI conversion tests

**File:** `tests/N64GraphicsTest.cpp` (append)

- `raw2ci_torch` with CI8 (depth=8): 1-byte per pixel → index passthrough
- `raw2ci_torch` with CI4 (depth=4): packed nibbles → verify high/low nibble extraction
- `ci2raw_torch` round-trip with CI8

---

## Commit 5: NAudio v1 AudioContext string methods

**File:** `tests/NAudioContextTest.cpp`

Pure switch-based string conversions, no dependencies.

- `GetMediumStr(0)` → "Ram", `(1)` → "Unk", `(2)` → "Cart", `(3)` → "Disk", `(4)` → "RamUnloaded", `(5)` → ERROR check, `(255)` → "ERROR"
- `GetCachePolicyStr(0)` → "Temporary", `(1)` → "Persistent", `(2)` → "Either", `(3)` → "Permanent", `(4+)` → "ERROR"
- `GetCodecStr(0)` → "ADPCM", ..., `(7)` → "UNK7", `(8+)` → "ERROR"

---

## Commit 6: NAudio v1 data structure tests

**File:** `tests/NAudioDataTest.cpp`

- `AudioTableEntry`: construct with known values, verify all fields
- `AudioTableData`: construct with medium, addr, type, entries vector
- `EnvelopePoint`: construct with delay/arg
- `EnvelopeData`: construct with point vector
- `AudioTableType` enum values exist (SAMPLE_TABLE, SEQ_TABLE, FONT_TABLE)

---

## Commit 7: SF64 Skeleton/Message data structure tests

**File:** `tests/SF64DataTest.cpp`

- `SF64::LimbData`: construct with addr, dList, trans, rot, sibling, child, index — verify all fields
- `SF64::SkeletonData`: construct with limb vector, verify size and field access
- `SF64::SkeletonFactory::GetExporter()` has Code, Header, Binary
- `SF64::MessageData`: construct with opcode vector and string — verify both fields
- `SF64::MessageFactory::GetExporter()` has Code, Header, Binary, Modding, XML
- `SF64::MessageFactory::SupportModdedAssets()` returns true

---

## Commit 8: TextureFactory data structure tests

**File:** `tests/TextureFactoryTest.cpp`

- `TextureFormat` struct: construct with type and depth, verify fields
- `TextureData`: construct with format, width, height, buffer — verify all fields
- `TextureFactory::GetExporter()` has Code, Header, Binary
- Verify TextureFormat combinations match expected depth values (RGBA16→16, RGBA32→32, CI4→4, CI8→8, I4→4, I8→8, IA1→1, IA4→4, IA8→8, IA16→16)

---

## Notes

- Archive wrappers (SWrapper) are skipped — they require StormLib and do filesystem I/O, making them integration test territory.
- The `getCharByCode` function and `gASCIIFullTable`/`gCharCodeEnums` in SF64::MessageFactory are file-local and cannot be tested externally. If testing these is desired in the future, they could be moved to a header.
- n64graphics scaling macros (SCALE_5_8, etc.) are defined in the .c file and not directly testable from C++ tests, but their behavior is verified indirectly through the raw2rgba/rgba2raw round-trip tests.

---

## Critical Files

| File | Role |
|---|---|
| `tests/StrHash64Test.cpp` | New — CRC64 hash tests |
| `tests/N64GraphicsTest.cpp` | New — pixel format conversion tests |
| `tests/NAudioContextTest.cpp` | New — AudioContext string method tests |
| `tests/NAudioDataTest.cpp` | New — NAudio data structure tests |
| `tests/SF64DataTest.cpp` | New — SF64 skeleton/message tests |
| `tests/TextureFactoryTest.cpp` | New — texture data structure tests |
| `lib/strhash64/StrHash64.h` | Read-only reference |
| `lib/n64graphics/n64graphics.h` | Read-only reference |
| `src/factories/naudio/v1/AudioContext.h/cpp` | Read-only reference |
| `src/factories/naudio/v1/AudioTableFactory.h` | Read-only reference |
| `src/factories/naudio/v1/EnvelopeFactory.h` | Read-only reference |
| `src/factories/sf64/SkeletonFactory.h` | Read-only reference |
| `src/factories/sf64/MessageFactory.h` | Read-only reference |
| `src/factories/TextureFactory.h` | Read-only reference |

## Verification

After each commit:
```bash
cmake -B build -DBUILD_TESTS=ON && cmake --build build && build/torch_tests
```
