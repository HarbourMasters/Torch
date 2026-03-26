# Plan: Address test-review.md Feedback

## Context
The `unit-testing-discovery` branch added ~400 test cases across 31 unit test files and 15 integration tests. A review (`docs/test-review.md`) identified specific issues plus a systemic pattern: ~10 test files only verify struct construction (fields store what you put in) rather than testing actual parse logic. The SM64CollisionFactoryTest pattern—crafting byte buffers and reading via BinaryReader—should be applied to all tests where the factory has meaningful parse logic.

## Part A: Review-Specific Fixes

### A1. Fix `GetAsset()` fuzzy matching → exact suffix match
**File:** `tests/integration/SM64IntegrationTest.cpp:32-40`
Replace `key.find(name)` with `key == name || key.ends_with("/" + name)` (C++20).

### A2. Add comment to GrayscaleAlpha1bpp test
**File:** `tests/TextureUtilsTest.cpp:34-36`
Explain that `CalculateTextureSize` returns decoded buffer size (1 byte/pixel after expansion), not raw encoded size.

### A3. Remove duplicate DecompressorTest
**File:** `tests/DecompressorTest.cpp:46-50` — delete `GetCompressionTypeZeroOffset`.

### A4. Add non-palindrome endianness test
**File:** `tests/TextureUtilsTest.cpp` — add test with `0x8000` to exercise BSWAP16.

### A5. Move ANIMINDEX_COUNT macro to header
- `src/factories/sm64/AnimationFactory.h` — add macro
- `src/factories/sm64/AnimationFactory.cpp:6` — remove macro
- `tests/SM64AnimationTest.cpp:5-6` — remove local redefinition

### A6. Add CI skip comment
**File:** `.github/workflows/tests.yml` — comment explaining integration tests self-skip.

---

## Part B: Upgrade Struct Tests to BinaryReader Parse Tests

The pattern: craft a big-endian byte buffer representing the on-disk format, create a `LUS::BinaryReader`, read fields the same way the factory's `parse()` does, verify values. This tests the binary format contract.

### B1. MK64DataTest.cpp — HIGH VALUE (interesting parse logic)
**Add BinaryReader tests for:**
- **CourseVtx flag extraction** (lines 95-111 of CourseVtx.cpp): The parse extracts 4-bit flags from the low bits of cn1 and cn2 (`flags = (cn1 & 3) | ((cn2 << 2) & 0xC)`), then masks the color bytes (`cn1 & 0xfc`, `cn2 & 0xfc`). Craft a vertex where cn1=0x07, cn2=0x0B to verify flag=0x07&3=3, (0x0B<<2)&0xC=0x8, flags=0xB, colors masked to 0x04 and 0x08.
- **DrivingBehaviour sentinel** (lines 90-101 of DrivingBehaviour.cpp): Craft a buffer with 2 normal entries followed by the sentinel (w1=-1, w2=-1, id=0). Verify it reads exactly 3 entries and stops.
- **TrackSections size mismatch** (lines 89-108 of TrackSections.cpp): On-disk is 8 bytes (u32+i8+i8+u16) but struct has u64 crc field. Verify that reading 8-byte on-disk entries produces correct struct values, particularly that the u32 addr maps to the u64 crc field.
- **Paths parse**: 8 bytes per entry (3×int16_t + uint16_t)
- **SpawnData parse**: 8 bytes per entry (3×int16_t + uint16_t)
- **ItemCurve parse**: 100 bytes of uint8_t read in sequence

### B2. DisplayListFactoryTest.cpp — MEDIUM VALUE
**Add BinaryReader test for GBI command pairs:**
Craft a minimal display list: `gsSPVertex` (opcode 0x01) + `gsSPEndDisplayList` (opcode 0xDF). Read w0/w1 pairs with big-endian BinaryReader. Verify opcode extraction and word values. The existing opcode extraction test already does bit-shifting on constants—the BinaryReader test adds format verification.

### B3. SM64GeoLayoutTest.cpp — MEDIUM VALUE
**Add byte-level command parsing tests:**
The GeoLayout parser uses raw pointer arithmetic with `cur_geo_cmd_*` macros that read from a `cmd` pointer with BSWAP. We can test this pattern by crafting byte buffers for simple commands:
- **End command** (opcode 0x00): single byte, no args
- **OpenNode** (opcode 0x04): 4 bytes, no args
- **NodeTranslation** (opcode 0x10): 12 bytes = opcode + drawingLayer(u8) + pad(u16) + x,y,z(int16_t each) + pad(u16) + displayList(u32)

Read bytes using the same BSWAP16/BSWAP32 logic the macros use, verify field values.

### B4. SM64BehaviorScriptTest.cpp — MEDIUM VALUE
**Add byte-level command parsing tests:**
Similar to GeoLayout. Craft bytes for:
- **BEGIN** (opcode 0x00): 4 bytes, reads u8 at offset 1
- **SET_INT** (opcode 0x27): 4 bytes, reads u8 field + s16 value
- **CALL** (opcode 0x02): 8 bytes, reads u32 address at offset 4

### B5. FZXDataTest.cpp — MEDIUM VALUE
**Add BinaryReader test for EADAnimation header:**
28 bytes: int16_t frameCount + int16_t limbCount + 6×uint32_t pointers. Craft big-endian buffer, read sequentially, verify all fields.

### B6. NAudioDataTest.cpp — MEDIUM VALUE
**Add BinaryReader test for AudioTableEntry:**
The on-disk format is: u32 addr + u32 size + i8 medium + i8 policy + i16 sd1 + i16 sd2 + i16 sd3 = 16 bytes per entry. Craft a buffer with the 16-byte header (i16 count + i16 medium + u32 addr + 8 pad bytes) followed by entries.

### B7. SF64DataTest.cpp — MEDIUM VALUE
**Add BinaryReader test for limb data:**
Each limb is 0x20 (32) bytes: u32 dList + 3×float trans + 3×int16_t rot + i16 pad + u32 sibling + u32 child. Craft a single limb buffer and verify all fields parse correctly.

### Tests that STAY as struct-only (factory parse needs Companion):
- **PM64DataTest.cpp** — PM64ShapeFactory::parse has complex ByteSwapShapeData that processes entire shape files with address translation. Not feasible without Companion.
- **ViewportFactoryTest.cpp, LightsFactoryTest.cpp, FloatFactoryTest.cpp, VtxFactoryTest.cpp** — Already have BinaryReader parse tests. No changes needed.

---

## Implementation Order

1. Part A fixes (A1-A6) — straightforward edits
2. Part B high-value (B1: MK64 flag extraction, sentinel, size mismatch)
3. Part B medium-value (B2-B7: remaining BinaryReader upgrades)
4. Build and run tests

## Verification
```bash
cmake -H. -Bbuild -GNinja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_INTEGRATION_TESTS=ON
cmake --build build -j
build/torch_tests
```
