# Extract AliasManager from Companion

## Context

Alias logic (for Set_ DList copies) is currently scattered across Companion:
- `gPendingAliases` map stored on Companion
- `RegisterAssetAlias()` public method on Companion
- Inline alias writing block in Companion's Binary export loop
- Called from OoTSceneFactory::ResolveGfxWithAlias

This adds cognitive load when reading Companion — alias handling is OoT-specific
behavior leaking into shared infrastructure. An AliasManager singleton encapsulates
all alias logic in one place.

## Why aliases exist (cannot be eliminated)

DLists embed a CRC64 of their own output path ("bhash"). Set_ DLists need to be
byte-identical copies of the base DList (same bhash). Re-parsing the DList under
a Set_ path produces a different bhash → binary mismatch. Aliases are copies,
not independent assets.

## Implementation

### New file: src/AliasManager.h
```cpp
class AliasManager {
public:
    static AliasManager* Instance;
    void Register(const std::string& primaryPath, const std::string& aliasPath);
    void WriteAliases(const std::string& primaryPath, BinaryWrapper* wrapper,
                      const std::vector<char>& data);
    void Clear();
private:
    std::unordered_map<std::string, std::vector<std::string>> mAliases;
};
```

### Changes

1. **Companion.h**: remove `gPendingAliases`, remove `RegisterAssetAlias()`
2. **Companion.cpp**:
   - Init: create AliasManager instance
   - Export loop: replace inline alias block with
     `AliasManager::Instance->WriteAliases(result.name, wrapper, dataVec)`
   - End: `AliasManager::Instance->Clear()`
3. **OoTSceneFactory.cpp**: change `Companion::Instance->RegisterAssetAlias(path, expectedPath)`
   to `AliasManager::Instance->Register(path, expectedPath)`

## Verification
1. `cmake --build build -j32`
2. `test_assets.py` → 35,386/35,386
3. Zero behavioral change — just refactoring ownership

