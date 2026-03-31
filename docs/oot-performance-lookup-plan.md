# Eliminate YAML::Node from lookup infrastructure

## Context
perf profiling shows ~60% of O2R generation time is memory allocation/page faults from
YAML::Node copies. The hot-path lookup functions (GetNodeByAddr, GetSafeStringByAddr,
SearchVtx) return `tuple<string, YAML::Node>` from gAddrMap, which copies the YAML::Node
on every return. Factory DX should stay unchanged — factories still receive YAML::Node
for parse/export. Only the lookup infrastructure changes.

## Key finding: callers don't need YAML::Node from lookups
Audited every GetNodeByAddr call site across all factories:
- **Most** just read `symbol` from the node (fzerox, sf64, sm64, DisplayListOverrides)
- **Some** check `type` then use `path` (DisplayListFactory VTX handler)
- **Several** just check existence (`!decl.has_value()`) and don't use the node at all
- **None** access factory-specific fields from lookup results

## Approach: Lightweight lookup index alongside gAddrMap

### New struct: `AssetLookup`
```cpp
struct AssetLookup {
    std::string path;
    std::string type;
    std::string symbol;
    uint32_t offset = 0;
    uint32_t count = 0;
};
```
No YAML::Node, no CRC64. Just the fields callers actually need. Trivially copyable
(strings only), no shared_ptr ref counting.

### New map: `gLookupMap`
```cpp
unordered_map<string, unordered_map<uint32_t, AssetLookup>> gLookupMap;
```
Populated alongside gAddrMap during registration. The lookup functions query this
instead of gAddrMap. gAddrMap still exists for code that needs the full YAML::Node
(ProcessFile export loop, AddAsset type checking).

### Changed function signatures
```cpp
// Fast path: returns lightweight struct (cheap copy, no YAML::Node)
std::optional<AssetLookup> GetNodeByAddr(uint32_t addr);
std::optional<AssetLookup> GetSafeNodeByAddr(uint32_t addr, std::string type);
std::optional<std::vector<AssetLookup>> GetNodesByType(const std::string& type);

// Full access: returns YAML::Node for rare internal use (AddAsset, etc.)
std::optional<std::tuple<std::string, YAML::Node>> GetFullNodeByAddr(uint32_t addr);
```

GetSafeStringByAddr and GetStringByAddr stay unchanged (already return string).

### Factory call sites update
All `std::get<0>(dec.value())` → `dec->path`
All `std::get<1>(dec.value())` then `GetSafeNode(node, "symbol")` → `dec->symbol`
All `GetTypeNode(std::get<1>(dec.value()))` → `dec->type`

### What stays on YAML::Node
- Factory `parse(buffer, node)` — unchanged, factories still get YAML::Node
- Factory `Export(stream, data, name, node, replacement)` — unchanged
- ParseResultData stores YAML::Node — unchanged
- gAddrMap stores YAML::Node — unchanged (needed by AddAsset and export loop)
- Decompressor::AutoDecode takes YAML::Node — unchanged (including synthetic nodes)

### What changes
- gLookupMap added alongside gAddrMap (populated at same time)
- gTypeIndex changes from `vector<tuple>` to `vector<AssetLookup*>`
- GetNodeByAddr, GetSafeNodeByAddr, GetNodesByType return AssetLookup
- SearchVtx returns AssetLookup (reads offset/count from struct, not YAML)
- DisplayListOverrides VTX overlap map stores AssetLookup
- All factory call sites that do `std::get<0/1>` → use `.path`/`.symbol`/`.type`
- GetSafeStringByAddr/GetSafeNodeByAddr use `.type` for validation (no YAML access)

## Implementation order
1. Define AssetLookup struct in new header `src/AssetLookup.h`
2. Add gLookupMap to Companion.h, populate in registration (line 754) and RegisterAsset
3. Update GetNodeByAddr to query gLookupMap instead of gAddrMap
4. Update GetSafeNodeByAddr, GetSafeStringByAddr, GetNodesByType
5. Update SearchVtx to use AssetLookup fields
6. Update DisplayListOverrides (VTX overlap map, override functions)
7. Update all factory call sites (`std::get<0/1>` → `.path`/`.symbol`)
8. Update RemapSegmentedAddr to use gLookupMap
9. Build, test, measure

## Files to modify
- `src/AssetLookup.h` (new) — struct definition
- `src/Companion.h` — add gLookupMap, change method signatures
- `src/Companion.cpp` — registration, all lookup methods
- `src/factories/DisplayListFactory.cpp` — SearchVtx, RemapSegmentedAddr, export
- `src/factories/DisplayListOverrides.h/.cpp` — VTX overlap, override functions
- `src/factories/oot/DeferredVtx.cpp` — VTX overlap registration
- `src/factories/{sm64,sf64,fzerox,naudio}/*.cpp` — `std::get<>` → field access
- `src/factories/AssetArrayFactory.cpp` — same

## What this does NOT change
- Factory parse() interface — still receives YAML::Node
- Factory Export() interface — still receives YAML::Node
- ParseResultData — still stores YAML::Node
- Decompressor::AutoDecode — still takes YAML::Node
- Any factory's internal YAML access patterns
- Developer experience for writing new factories

## Expected impact
- Eliminates YAML::Node copies from all lookup returns (~500K+ per run)
- Eliminates YAML `node["type"]` access in GetSafeStringByAddr/GetSafeNodeByAddr
- Eliminates YAML `node["offset"]`/`node["count"]` access in SearchVtx
- Should significantly reduce the memory allocation churn shown in perf
- Factory DX unchanged — this is purely infrastructure

## Verification
- `cmake --build build -j32`
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
- Compare timing before/after (target: measurable improvement from 25s baseline)
- Test SM64 with Ghostship if available for cross-port regression
