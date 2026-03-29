# Plan: Set_ DList Duplicates for Scene Alternate Headers

## Context
OoT scenes with alternate headers (child/adult, day/night variants) reference the same DLists as primary headers under different names. For example, `bdan_room_0DL_002CD8` (primary) and `bdan_room_0Set_0000E0DL_002CD8` (alternate) point to the same ROM data. OTRExporter creates both files with identical binary content. Torch currently only creates the primary, resulting in 2,388 missing `Set_*DL_*` files.

The root cause is twofold: (1) `baseName` mechanism makes alternate headers use parent names for DList symbols, so they never compute `Set_`-prefixed symbols; (2) even if they did, `AddAsset` rejects duplicate offsets, so no second file is created.

## Approach: Alias Registration + baseName fix for DLists only

### Part 1: Scene Factory — Use `entryName` for DLists, keep `baseName` for others

Currently `baseName` (parent's name) is used for ALL sub-asset naming in alternate headers. But:
- DLists need `entryName` (the Set_ name) — OTRExporter creates separate Set_ DList files
- Backgrounds need `baseName` (parent name) — already verified working
- Cutscenes need `entryName` — already verified working
- Pathways need `baseName` — already verified working

**Change**: In SetMesh handler, use `entryName` instead of `baseName` for DList symbol generation. This means the 4 `MakeAssetName(baseName, "DL", ...)` calls in SetMesh become `MakeAssetName(entryName, "DL", ...)`.

After `ResolveGfxPointer` returns the primary's path (because the DList at that offset already exists), detect the name mismatch and register an alias:

```cpp
std::string opaSymbol = MakeAssetName(entryName, "DL", opaOffset);
opaPath = ResolveGfxPointer(opaAddr, opaSymbol, buffer);
// If ResolveGfxPointer returned the primary's path but we wanted a Set_ path,
// register an alias so the duplicate file gets created during export.
std::string expectedOpaPath = currentDir + "/" + opaSymbol;
if (!opaPath.empty() && opaPath != expectedOpaPath) {
    Companion::Instance->RegisterAssetAlias(opaPath, expectedOpaPath);
    opaPath = expectedOpaPath;  // Write Set_ path into the command binary
}
```

### Part 2: Companion — Minimal alias mechanism

**Companion.h**: Add member and method:
```cpp
std::unordered_map<std::string, std::vector<std::string>> gPendingAliases;
void RegisterAssetAlias(const std::string& primaryPath, const std::string& aliasPath);
```

**Companion.cpp**:
- `RegisterAssetAlias` stores `gPendingAliases[primaryPath].push_back(aliasPath)`
- In `ProcessFile` export loop (after line 832 `AddFile`), write aliases:
  ```cpp
  if (Torch::contains(this->gPendingAliases, result.name)) {
      for (auto& alias : this->gPendingAliases[result.name]) {
          this->gCurrentWrapper->AddFile(alias, std::vector(data.begin(), data.end()));
      }
      this->gPendingAliases.erase(result.name);
  }
  ```

### Files to Modify
1. `src/Companion.h` — add `gPendingAliases` member + `RegisterAssetAlias` declaration
2. `src/Companion.cpp` — implement `RegisterAssetAlias`, add alias writing in export loop
3. `src/factories/oot/OoTSceneFactory.cpp` — change DList naming to `entryName`, add alias detection after each `ResolveGfxPointer` call in SetMesh (4 call sites for mesh types 0/2, 2 for mesh type 1)

### Why This Works
- **Parse phase**: Alt header processes SetMesh, computes `Set_` DList symbol, `ResolveGfxPointer` returns primary's path, alias registered
- **Export phase**: When primary DList is exported, `AddFile` writes it, then aliases are checked and duplicate files written with same binary data
- **Zero memory overhead**: Aliases are just string mappings; binary data is already in scope during export
- **No changes to AddAsset/gAddrMap**: Core dedup logic untouched

### Verification
1. `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --failures-only` — should show ~2,388 fewer missing
2. `python3 soh/tools/compare_asset.py scenes/nonmq/bdan_scene/bdan_room_0Set_0000E0DL_002CD8` — should pass
3. Objects, overlays, code, textures should remain 100% (no regressions)
