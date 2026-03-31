# Pre-extract YAML fields to eliminate node access overhead

## Context
O2R gen takes ~25s. OTRExporter does <10s. Profiler shows ~25% of time in YAML node
access (regex matching + linear key search on every `node["key"]` call). OTRExporter is
fast because ZAPDTR pre-parses XML into C++ structs — YAML/XML is never touched during
the hot path. Torch passes `YAML::Node` everywhere and accesses fields repeatedly.

The core data structure is `gAddrMap`:
```cpp
unordered_map<string, unordered_map<uint32_t, tuple<string, YAML::Node>>> gAddrMap;
```

Every address resolution goes through this and then accesses `node["type"]`,
`node["offset"]`, `node["count"]`, `node["symbol"]` etc. Each of these is an
expensive yaml-cpp operation.

## Approach: Cache commonly accessed fields in a struct

Replace the tuple with a struct that pre-extracts hot fields:

```cpp
struct AssetEntry {
    std::string path;
    std::string type;
    std::string symbol;
    uint32_t offset = 0;
    uint32_t count = 0;
    uint64_t crc = 0;        // pre-computed CRC64 of path
    YAML::Node node;          // keep for factory-specific fields
};
```

Populate these fields ONCE during registration (line 754) and AddAsset/RegisterAsset.
Then the hot path reads `entry.type` instead of `node["type"]`.

## Key call sites that benefit

1. **GetSafeStringByAddr** (`Companion.cpp:1800`) — called per-command for G_VTX, G_DL,
   G_SETTIMG, G_MTX. Currently does `GetTypeNode(n)` → `n["type"]` for validation.
   With struct: just compare `entry.type`.

2. **GetSafeNodeByAddr** (`Companion.cpp:1782`) — same pattern.

3. **GetNodesByType** (`Companion.cpp:1872`) — iterates type index checking
   `node["autogen"]`. With struct: check `entry.autogen` bool.

4. **SearchVtx** (`DisplayListFactory.cpp:241`) — accesses `node["offset"]`,
   `node["count"]` per VTX asset. With struct: `entry.offset`, `entry.count`.

5. **DListBinaryExporter::Export** (`DisplayListFactory.cpp:312`) — calls CRC64 per
   resolved reference. With struct: pre-computed `entry.crc`.

6. **DisplayListOverrides** — override functions access `node["offset"]`, `node["count"]`,
   `node["symbol"]`. With struct: direct field access.

## Implementation steps

### Step 1: Define AssetEntry struct
Add to `Companion.h`, replacing the tuple typedef.

### Step 2: Update gAddrMap type
Change from `tuple<string, YAML::Node>` to `AssetEntry`.
This touches every file that accesses gAddrMap entries via `std::get<0>()` / `std::get<1>()`.

### Step 3: Populate fields at registration
- Line 754 (initial YAML registration): extract type, symbol, offset, count, compute CRC64
- RegisterAsset (line 1574): same extraction for auto-discovered assets

### Step 4: Update hot-path accessors
- GetSafeStringByAddr: use `entry.type` instead of `GetTypeNode(node)`
- GetSafeNodeByAddr: same
- GetNodesByType: use `entry.type`, check `entry.autogen` bool
- SearchVtx: use `entry.offset`, `entry.count`

### Step 5: Update DList export to use cached CRC64
- Each CRC64(path) call → `entry.crc`

### Step 6: Update all `std::get<>` call sites
Search for `std::get<0>` and `std::get<1>` on gAddrMap entries throughout codebase.
Change to `entry.path` and `entry.node`.

## Scope of changes
- `src/Companion.h` — AssetEntry struct, gAddrMap type change
- `src/Companion.cpp` — registration, all accessor methods
- `src/factories/DisplayListFactory.cpp` — SearchVtx, Export, parse
- `src/factories/DisplayListOverrides.cpp` — override functions
- `src/factories/oot/*.cpp` — any OoT factories that access gAddrMap entries

## Expected impact
- Eliminates YAML regex/search overhead (~25% of runtime) from hot path
- Eliminates redundant CRC64 computation (~4% of runtime)
- Conservative estimate: 25s → 15-18s
- Combined with skip_hash and STORE compression: could approach 12-15s
- Further gains would require parallelization

## Verification
- `cmake --build build -j32`
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
- Compare timing before/after
