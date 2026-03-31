# Second Pass: Minimize OoT pollution in shared files

## Context
First pass gated OoT code with runtime checks and `#ifdef OOT_SUPPORT`. Two problems:
1. `#ifdef OOT_SUPPORT` is fake — BUILD_OOT defaults ON for all builds
2. Functions like RemapSegmentedAddr live in shared files with internal "if not OoT, return" — they should just not exist in shared files

Goal: shared files should have minimal, easy-to-follow OoT additions matching the existing PM64 pattern (small runtime minor version checks).

## Changes

### 1. Move RemapSegmentedAddr to Companion (method)
**Why**: It calls Companion APIs exclusively. Companion already has game-gated address logic (PatchVirtualAddr, GetNodeByAddr). Consolidates all address-resolution in one place.

- **Companion.h**: Add `uint32_t RemapSegmentedAddr(uint32_t addr, const std::string& expectedType = "");`
- **Companion.cpp**: Add method body (move from DisplayListFactory.cpp). Change `Companion::Instance->` to `this->`. Keep internal OoT guard.
- **DisplayListFactory.cpp**: Delete static function (~30 lines). Update 3 call sites: `RemapSegmentedAddr(x, y)` → `Companion::Instance->RemapSegmentedAddr(x, y)`

### 2. Remove `#ifdef OOT_SUPPORT` from shared files
**Why**: BUILD_OOT defaults ON, so these aren't real gates. The DeferredVtx calls are already runtime no-ops when BeginDefer() hasn't been called.

- **DisplayListFactory.h**: Remove `#ifdef`/`#endif` around DeferredVtx.h include → unconditional include
- **DisplayListFactory.cpp**: Remove all 5 `#ifdef OOT_SUPPORT`/`#endif` pairs. Code inside stays as-is (guarded by `IsDeferred()`)
- **oot/DeferredVtx.h**: Add `#ifndef OOT_SUPPORT` inline no-op stubs so it links when BUILD_OOT=OFF:
  ```cpp
  #ifdef OOT_SUPPORT
    // real declarations (existing)
  #else
    inline bool IsDeferred() { return false; }
    inline std::vector<PendingVtx> SaveAndClearPending() { return {}; }
    inline void RestorePending(std::vector<PendingVtx>&) {}
    inline void AddPending(uint32_t, uint32_t) {}
    inline void FlushDeferred(const std::string&) {}
  #endif
  ```
  The `#ifdef` is acceptable here — it's inside the oot/ header, not in shared code.

### 3. No changes to Companion.cpp PatchVirtualAddr/GetNodeByAddr
The existing `gConfig.gbi.subversion == GBIMinorVersion::OoT` checks are small, follow the PM64 pattern, and belong in Companion where all address resolution lives.

### 4. No changes to gSunDL/sShadowMaterialDL hacks
Already cleanly gated behind `GetGBIMinorVersion() == GBIMinorVersion::OoT && replacement->find("symbolName")`. Same pattern as PM64's hack (line 628).

### 5. No changes to SearchVtx OOT:ARRAY addition
2-line runtime check following established pattern.

## Net effect on shared files
- **DisplayListFactory.cpp**: -30 lines (RemapSegmentedAddr), -10 lines (5 ifdef pairs), 3 trivial call site updates
- **DisplayListFactory.h**: -2 lines (ifdef/endif removed)
- **Companion.h**: +1 line (method declaration)
- **Companion.cpp**: +30 lines (method body — alongside existing game-specific address logic)

## What remains in DisplayListFactory.cpp after this pass
- SearchVtx: 2-line OoT minor check (matches PM64 pattern)
- gSunDL/sShadowMaterialDL: cleanly gated behind minor + symbol name
- DeferredVtx calls: no ifdefs, just `if (IsDeferred())` runtime checks (no-op for non-OoT)
- OOT:ARRAY type check on line 461: inside gSunDL block (already OoT-gated)

## Verification
1. `cmake --build build -j32` (BUILD_OOT=ON, default)
2. `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
3. Optional: `cmake -DBUILD_OOT=OFF` build to verify no-op stubs link
