# P1 Testing Implementation Plan

## Context

P0 tests are complete (88 tests across BinaryReader/Writer, StringHelper, TorchUtils, Decompressor). This plan covers P1: Vec3D types, TextureUtils, VtxFactory, DisplayListFactory, and CompTool.

Key finding: VtxFactory and DisplayListFactory both depend heavily on `Companion::Instance` (the global singleton) and `Decompressor::AutoDecode`. Testing their `parse()` methods requires either initializing Companion or restructuring to test data structures and exporters in isolation. This plan focuses on what's testable now and notes where Companion coupling blocks further coverage.

---

## Commit 1: Vec3D construction and field access

**File:** `tests/Vec3DTest.cpp`

- `Vec3f`: construct with (1.0, 2.0, 3.0), verify x/y/z fields
- `Vec3f`: default construction → (0, 0, 0)
- `Vec3s`: construct with (100, -200, 300), verify x/y/z
- `Vec3i`: construct with known int32 values
- `Vec3iu`: construct with known uint32 values
- `Vec2f`: construct with (1.0, 2.0), verify x and z (note: second field is `z`, not `y`)
- `Vec4f`: construct with 4 floats, verify all fields
- `Vec4s`: construct with 4 int16_t values, verify all fields

---

## Commit 2: Vec3D width, precision, and stream output

**File:** `tests/Vec3DTest.cpp` (append)

- `Vec3f::precision()`: known float values → expected decimal places (0-12)
- `Vec3f::width()`: verify width = max_magnitude + 1 + precision
- `Vec3s::width()`: verify width = max_magnitude of components
- Stream output: write Vec3f to `std::ostringstream`, verify formatted string
- Stream output: write Vec3s to `std::ostringstream`
- Edge cases: zero values, negative values, large values

---

## Commit 3: TextureUtils CalculateTextureSize

**File:** `tests/TextureUtilsTest.cpp`

For a fixed 32x32 texture, verify expected byte sizes:
- `RGBA32bpp`: 32 * 32 * 4 = 4096
- `RGBA16bpp`: 32 * 32 * 2 = 2048
- `GrayscaleAlpha16bpp`: 32 * 32 * 2 = 2048
- `TLUT`: 32 * 32 * 2 = 2048
- `Grayscale8bpp`: 32 * 32 = 1024
- `Palette8bpp`: 32 * 32 = 1024
- `GrayscaleAlpha8bpp`: 32 * 32 = 1024
- `GrayscaleAlpha1bpp`: 32 * 32 = 1024
- `Palette4bpp`: 32 * 32 / 2 = 512
- `Grayscale4bpp`: 32 * 32 / 2 = 512
- `GrayscaleAlpha4bpp`: 32 * 32 / 2 = 512
- `Error`: 0

Also test non-square dimensions (64x16) and 1x1 edge case.

---

## Commit 4: TextureUtils alloc_ia8_text_from_i1

**File:** `tests/TextureUtilsTest.cpp` (append)

- Known 16-bit input where all bits set → all output bytes are 0xFF
- Known 16-bit input where no bits set → all output bytes are 0x00
- Mixed bit pattern → verify specific output bytes
- Small dimensions (16x1 = 1 uint16_t input)

---

## Commit 5: VtxFactory data structures

**File:** `tests/VtxFactoryTest.cpp`

Since VtxFactory::parse() requires Companion + Decompressor, test what we can without them:
- `VtxRaw`: verify struct size is 16 bytes (`sizeof(VtxRaw)`)
- `VtxData`: construct with known VtxRaw vector, verify mVtxs field access
- `VtxFactory::GetAlignment()` returns 8
- Parse a vertex manually from a BinaryReader (same logic as parse() but without Companion), verify all fields: ob[3], flag, tc[2], cn[4]

---

## Commit 6: DisplayListFactory data structures

**File:** `tests/DisplayListFactoryTest.cpp`

Since DListFactory::parse() requires Companion + GBI version + asset discovery, test what we can:
- `DListData`: construct with known uint32_t vector, verify mGfxs
- `DListData`: default empty construction
- `DListFactory::GetAlignment()` returns 8
- GBI opcode extraction: verify `(w0 >> 24) & 0xFF` logic for known commands

**Not tested (requires Companion):** parse(), code exporter (uses libgfxd), binary exporter, header exporter.

---

## Notes on Companion-dependent tests

VtxFactory::parse() and DListFactory::parse() both call:
1. `Decompressor::AutoDecode(node, buffer)` → which calls `Companion::Instance->GetCurrCompressionType()`
2. Various Companion methods for asset registration

To test these in the future, options include:
- Initialize a minimal Companion with synthetic ROM data and `ArchiveType::None`
- Extract parsing logic into standalone functions that take a BinaryReader directly
- Create a test fixture that sets up Companion::Instance

This is deferred to a future commit to avoid scope creep.

---

## Critical Files

| File | Role |
|---|---|
| `tests/Vec3DTest.cpp` | New — Vec3D type tests |
| `tests/TextureUtilsTest.cpp` | New — texture size calculation tests |
| `tests/VtxFactoryTest.cpp` | New — vertex data structure tests |
| `tests/DisplayListFactoryTest.cpp` | New — display list data structure tests |
| `src/types/Vec3D.h/cpp` | Read-only reference |
| `src/utils/TextureUtils.h/cpp` | Read-only reference |
| `src/factories/VtxFactory.h/cpp` | Read-only reference |
| `src/factories/DisplayListFactory.h/cpp` | Read-only reference |

## Verification

After each commit:
```bash
cmake -B build -DBUILD_TESTS=ON && cmake --build build && build/torch_tests
```
