# O2R generation performance (25s for 35,386 assets)

## Context
O2R generation takes ~25s. Compression was not the bottleneck (switching to STORE saved <1s).
Per-file timing shows 96% of time is in ProcessFile. The slowest files are DList-heavy
(objects, scene rooms) and overlay files with external references to large YAMLs like
gameplay_keep (6089 lines).

## Measured breakdown
- YAML loading: 60ms (negligible)
- ProcessFile: 24,800ms (96%)
- Archive close: 29ms (negligible)
- Top 20 slowest files: ~4.5s (objects + scene rooms with many DLists)
- Long tail (941 files × ~21ms avg): ~20s

## Root cause analysis
The hot path is DList parsing/export, specifically these O(n) lookups called per-command:

1. **SearchVtx** (`DisplayListFactory.cpp:235`): Called for every unresolved G_VTX.
   Calls `GetNodesByType("VTX")` which does a full linear scan of all assets in gAddrMap,
   builds a fresh vector copy, then iterates it checking address ranges. With thousands of
   VTX assets per file, this is O(n²).

2. **GetNodeByAddr** (`Companion.cpp:1642`): Called from AddAsset, GetSafeStringByAddr, etc.
   Fast path is a hash lookup, but the OoT cross-segment fallback (lines 1657-1670) iterates
   ALL stored addresses. Fires constantly for overlay files with segment aliasing (8-13).

3. **External file lookups**: Files with `external_files: gameplay_keep.yml` search that
   6089-line file's addr map on every miss. z_fbdemo_circle (7 assets) takes 348ms because
   of this.

## Plan: Index-based lookups

### Fix 1: Cache GetNodesByType results
`GetNodesByType()` (`Companion.cpp:1826`) scans gAddrMap every call. Add a type index:
```
gTypeIndex[file][type] = vector<tuple<string, YAML::Node>>
```
Built once during address registration (line 754), invalidated when AddAsset adds new entries.
SearchVtx becomes O(1) lookup + O(k) iteration over just VTX assets.

**Files:** `src/Companion.cpp`, `src/Companion.h`

### Fix 2: Index absolute addresses for cross-segment lookup
The cross-segment scan converts every stored address to absolute and compares. Pre-build
an absolute address index:
```
gAbsAddrIndex[file][absAddr] = tuple<string, YAML::Node>
```
Built during address registration for files with virtual addr maps. GetNodeByAddr's
fallback becomes O(1) instead of O(n).

**Files:** `src/Companion.cpp`, `src/Companion.h`

### Implementation order
1. Fix 1 (type index) — likely bigger impact since SearchVtx is called per-G_VTX
2. Fix 2 (abs addr index) — helps overlay files specifically
3. Re-measure to see remaining hotspots

## Verification
- `cmake --build build -j32`
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
- Compare timing: target <15s (40% improvement)
