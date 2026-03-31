# Add GBI minor version for OoT (OoT/MM)

## Context
CrashSquare requested a new GBI minor for OoT so that OoT/MM-specific DList hacks are
gated behind a proper version check instead of symbol-name matching. This keeps game-specific
behavior cleanly separated from other Torch-supported games.

Currently there are 3 OoT-specific hacks in DisplayListFactory.cpp that check symbol names:
1. `gSunDL` VTX override (lines 435-486) — ranged texture lookup for vertex commands
2. `sShadowMaterialDL` BSS texture hack (lines 762-772) — segment 0xC runtime reference
3. `gSunDL` texture format adjustment (lines 775-803) — I4/IA16 mismatch fix

These should be gated behind `GBIMinorVersion::OoT` instead of string matching.

## Changes

### 1. Add OoT enum value
**File**: `src/Companion.h` (line ~35)
```cpp
enum class GBIMinorVersion {
    None, Mk64, SM64, PM64,
    OoT  // OoT/MM (Ship of Harkinian)
};
```

### 2. Add config parsing
**File**: `src/Companion.cpp` (line ~1262, in GBI key parsing)
```cpp
} else if (key == "F3DEX2_OoT") {
    this->gConfig.gbi.version = GBIVersion::f3dex2;
    this->gConfig.gbi.subversion = GBIMinorVersion::OoT;
}
```

### 3. Gate existing hacks behind minor version
**File**: `src/factories/DisplayListFactory.cpp`

Replace symbol-name checks with minor version checks:
- Line 438: `replacement->find("gSunDL")` → `GetGBIMinorVersion() == GBIMinorVersion::OoT && replacement->find("gSunDL")`
- Line 764: `replacement->find("sShadowMaterialDL")` → `GetGBIMinorVersion() == GBIMinorVersion::OoT && replacement->find("sShadowMaterialDL")`
- Line 778: `replacement->find("gSunDL")` → `GetGBIMinorVersion() == GBIMinorVersion::OoT && replacement->find("gSunDL")`

Note: Keep the symbol-name checks too — the minor gates the game, the symbol-name
targets the specific DList. Both conditions should be true.

### 4. Update YAML config
**File**: `soh/assets/yml/config.yml`
Change `gbi: F3DEX2` to `gbi: F3DEX2_OoT` for all OoT ROM entries.

## Files to modify
- `src/Companion.h` — add `OoT` to `GBIMinorVersion` enum
- `src/Companion.cpp` — add `F3DEX2_OoT` config parsing
- `src/factories/DisplayListFactory.cpp` — gate 3 hacks behind OoT minor
- `soh/assets/yml/config.yml` — use `F3DEX2_OoT`

## Verification
- `cmake --build build -j32`
- Regen YAMLs and run: `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
