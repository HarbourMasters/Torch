# O2R Generation Performance Findings

## Baseline
- ROM: PAL GC (0227d7), 35,386 assets, 961 YAML files
- Time: ~25s (with logging at CRITICAL)

## Phase breakdown
| Phase | Time | % |
|-------|------|---|
| YAML loading | 60ms | 0.2% |
| Config/setup per file | 1,115ms | 4.5% |
| **Parsing (factory)** | **23,951ms** | **95.8%** |
| Export (binary) | 232ms | 0.9% |
| Archive close | 29ms | 0.1% |

## Factory breakdown
| Factory | Time | Count | Avg |
|---------|------|-------|-----|
| OOT:ROOM | 17,713ms | 639 | 27.7ms |
| GFX | 15,496ms | 7,380 | 2.1ms |
| OOT:SKELETON | 5,187ms | 194 | 27.0ms |
| OOT:LIMB | 1,232ms | 3,983 | 0.3ms |
| OOT:SCENE | 558ms | 235 | 2.4ms |
| TEXTURE | 0ms | 8,989 | ~0ms |
| OOT:ARRAY | 0ms | 7,062 | ~0ms |

Note: ROOM/SKELETON times overlap with GFX — room parsing triggers DList parsing.

## DList stats
- 495,084 GBI commands across 7,380 DList parses (67 avg commands/DList)
- 3,443 DLists declared in YAML, 3,937 auto-discovered via AddAsset
- SearchVtx: 30,942 calls, only 165ms total — not a bottleneck

## Key finding
The cost is inherent: OoT has ~7,400 display lists whose GBI commands must be walked
for binary export. At ~48µs per command across 495K commands, the time is evenly
distributed — no single hot function dominates.

## What was NOT the bottleneck
- ZIP compression (STORE vs level 9): <1s difference
- SearchVtx / GetNodesByType: 165ms total
- Binary export: 232ms
- YAML loading: 60ms
- Archive I/O: 29ms

## Optimization opportunities
- Parallelizing file processing (multiple YAML files concurrently)
- Reducing per-command overhead (needs real profiler — perf/callgrind)
- The ~1.1s config/setup phase includes a redundant YAML reload per file (line 758)

## gprof results (top functions by self time)
| % | Function | Category |
|---|----------|----------|
| 11.8% | YAML::RegEx::MatchUnchecked | YAML node access |
| 8.3% | sha1::SHA1::processBlock | Hash tracking |
| 6.4% | mz_crc32 | ZIP CRC (even with STORE) |
| 6.4% | YAML std::__find_if | YAML key lookup (linear scan) |
| 3.9% | CRC64() | Asset path hashing (9 calls per DList in exporter) |
| 3.4% | yaz0_decode | YAZ0 decompression (1,003 calls) |
| ~6% | shared_ptr ref counting | YAML node overhead |
| ~4% | YAML scanning/parsing | Repeated YAML operations |

**Root cause: YAML node access is ~25-30% of total runtime.** Every `node["key"]`,
`GetSafeNode`, `GetTypeNode` triggers regex matching and linear key search internally.
With 495K GBI commands × multiple node accesses per command, this adds up.

**Secondary: Hashing is ~18%.** SHA1 (hash tracking), CRC64 (asset path hashing in binary
exporter), and mz_crc32 (ZIP CRC per file, even with STORE).

## Hash cache bug
Torch has an incremental build system (`torch.hash.yml`) that SHA1-hashes each YAML file
and skips unchanged files on subsequent runs. However, it **never works for Binary export**
because of this condition in `Companion.cpp:1134`:
```cpp
if (this->gConfig.exporterType != ExportType::Binary) {
    this->gHashNode[...]["extracted"][...] = true;
}
```
Binary exports never set the "extracted" flag to true, so `NodeHasChanges` always returns
true and every file is reprocessed on every run. This means the SHA1 hashing (~8% of runtime)
is pure overhead with no benefit for O2R generation.

## Potential optimizations (ranked by impact)
1. **Cache YAML node values** — extract needed fields once per asset into a struct,
   avoid repeated `node["key"]` access during hot loops
2. **Reduce SHA1/hash tracking** — investigate if hash checking can be disabled or deferred
3. **Pre-compute CRC64** — cache asset path hashes instead of recomputing per reference
4. **Parallelization** — process independent YAML files concurrently

## Failed approach: AssetEntry struct migration
Attempted replacing `tuple<string, YAML::Node>` in gAddrMap with a struct caching
type/symbol/offset/count fields. Result: **3x slower** (70s vs 25s) because:
- Copying YAML::Node by value triggers expensive shared_ptr ref counting
- The struct is larger than the tuple, increasing memory pressure
- Switching to pointer-based API (returning AssetEntry*) would fix the copies but
  requires changing ~50 call sites across 15+ files — massive refactor

The gprof percentages were misleading: gprof amplifies frequently-called small functions
due to its instrumentation overhead. The actual YAML access cost may be lower than 25%.

## Remaining approach to investigate
- Parallelization (biggest potential win, but complex)
- Profile with a sampling profiler (perf with proper permissions) for accurate data
- Targeted caching of specific hot values without changing the core data structure
