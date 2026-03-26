# Plan: SM64 Integration Test Coverage Gaps

## Context

Using `lcov` coverage reporting on `src/factories/sm64/`, integration tests currently cover **12.5% of lines** and **46.7% of functions**. Several factories have 0% coverage, and partially covered factories are missing entire code paths (non-quad movtex, env-map paintings, etc.). This plan identifies every gap and describes what new integration tests would cover them.

## Current Coverage Summary

| File | Lines | Functions | Status |
|------|-------|-----------|--------|
| AnimationFactory.cpp | 97.0% | 100% | Done |
| DialogFactory.cpp | 100% | 100% | Done |
| DictionaryFactory.cpp | 100% | 100% | Done |
| TextFactory.cpp | 100% | 100% | Done |
| PaintingMapFactory.cpp | 61.5% | 100% | Partial |
| TrajectoryFactory.cpp | 55.4% | 100% | Partial |
| PaintingFactory.cpp | 54.1% | 50.0% | Partial |
| MovtexQuadFactory.cpp | 53.3% | 50.0% | Partial |
| MacroFactory.cpp | 50.9% | 50.0% | Partial |
| MovtexFactory.cpp | 33.3% | 50.0% | Partial |
| CollisionFactory.cpp | 28.8% | 50.0% | Partial |
| BehaviorScriptFactory.cpp | 0% | 0% | Not covered |
| GeoLayoutFactory.cpp | 0.4% | 14.3% | Not covered |
| LevelScriptFactory.cpp | 0% | 0% | Not covered |
| WaterDropletFactory.cpp | 0% | 0% | Not covered |

Header-only files (GeoCommand.h, BehaviorCommand.h, LevelCommand.h, SpecialPresetNames.h, SurfaceTerrains.h, MacroPresets.h) are 0% — these contain large string maps and enum-to-string functions only exercised during Code export.

## Gap Analysis

### Tier 1: Zero Coverage (new YAML configs + tests needed)

#### BehaviorScriptFactory (0%)
- **What it parses**: Variable-length opcode-driven command stream defining entity behavior (BEGIN, CALL, SET_INT, SPAWN_CHILD, LOAD_ANIMATIONS, etc.)
- **Dependency risk**: HIGH — pointer arguments reference other behavior scripts, animations, and collision data via `Companion::GetNodeByAddr()`
- **Plan**: Add `test_behavior_script.yml` targeting a simple, self-contained behavior script (e.g., coin or 1-UP behavior). The test validates binary export: header type, command count, and total size. Since pointer resolution happens at export, we need the referenced assets (animations) included in the same config or already present.
- **YAML config**: Needs segment mapping for segment 0x13 (behavior bank). Use offset for a small behavior like `bhvCoin` or `bhvTree`.

#### GeoLayoutFactory (0.4%)
- **What it parses**: Hierarchical scene graph commands (OpenNode, CloseNode, NodeTranslation, NodeDisplayList, BranchAndLink, etc.)
- **Dependency risk**: HIGH — BranchAndLink/Branch commands auto-register child geo layouts, NodeDisplayList references GFX assets
- **Plan**: Add `test_geo_layout.yml` targeting a simple geo layout (e.g., a coin or simple object). The test validates the binary export produces a Blob type (GeoLayout exports as Blob). Need to include any referenced display lists in the config.
- **YAML config**: Needs segment mapping. Use a small object geo layout.

#### LevelScriptFactory (0%)
- **What it parses**: Level setup commands (EXECUTE, LOAD_MODEL_FROM_GEO, AREA, OBJECT_WITH_ACTS, TERRAIN, MACRO_OBJECTS, etc.)
- **Dependency risk**: VERY HIGH — references behavior scripts, geo layouts, collision, macros, display lists
- **Plan**: Add `test_level_script.yml`. This is the most dependency-heavy factory. A minimal test would target a small level script segment. The `count` field can limit how many commands are parsed to keep dependencies manageable.
- **YAML config**: Needs `count` field to limit parsing scope, plus segment mappings and all transitively-referenced assets.

#### WaterDropletFactory (0%)
- **What it parses**: Fixed 36-byte struct (flags, model ID, behavior pointer, motion params, size params)
- **Dependency risk**: MODERATE — one behavior pointer field resolved at export
- **Plan**: Add `test_water_droplet.yml` targeting a water droplet params struct. The test validates binary size (header + fixed struct). The behavior pointer needs its target behavior in the config.
- **YAML config**: Needs the referenced behavior script asset.

### Tier 2: Partial Coverage (missing code paths in existing tests)

#### CollisionFactory (28.8%)
- **What's covered**: Basic header validation and size check
- **What's missing**: Surface type parsing (force surfaces vs regular), special object parsing (with variable extra params based on preset type), environment/water box parsing
- **Plan**: The existing `test_collision.yml` uses `cannon_lid_collision` which is small. Add a second config `test_collision_complex.yml` targeting a level collision with special objects and water boxes (e.g., Bob-omb Battlefield or Jolly Roger Bay terrain). Add test assertions for command count being large enough to include specials/environment data.

#### MovtexFactory (33.3%)
- **What's covered**: Quad movtex path only (the `quad: true` config)
- **What's missing**: Non-quad movtex (requires `count` and `has_color` fields), both with-color and without-color sub-paths
- **Plan**: Add `test_movtex_nonquad.yml` with `quad: false`, `count`, and `has_color` fields targeting a non-quad moving texture (e.g., water flow in a level). Add `test_movtex_nonquad_color.yml` for the color variant if a suitable asset exists.

#### MacroFactory (50.9%)
- **What's covered**: Basic parse and binary export
- **What's missing**: The `MACRO_OBJECT_WITH_BEH_PARAM` code path (when `mMacroData[i+5] != 0`) — this only affects Code export, not Binary export
- **Plan**: Coverage gap is in the Code exporter, not binary. Since integration tests only test binary export, improving this requires either testing code export or accepting this as a unit-test-only gap. No new integration test config needed unless we add code export testing.

#### MovtexQuadFactory (53.3%)
- **What's covered**: Basic parse and binary export with valid quads
- **What's missing**: NULL quad pointers (quad.second == 0), missing node lookup paths
- **Plan**: Similar to MacroFactory — the gaps are in edge cases of the Code/Binary exporter's node resolution. Hard to trigger with real ROM data since valid quads always have valid pointers. Accept as-is or add a targeted unit test.

#### PaintingFactory (54.1%)
- **What's covered**: PAINTING_IMAGE texture type, RIPPLE_TRIGGER_PROXIMITY
- **What's missing**: PAINTING_ENV_MAP texture type, RIPPLE_TRIGGER_CONTINUOUS trigger, error paths for missing display list nodes
- **Plan**: Add `test_painting_envmap.yml` targeting an environment-mapped painting if one exists in SM64 US ROM. The env-map and continuous-ripple paths may not exist in the US ROM data — if so, these are dead code paths that can only be tested via unit tests.

#### PaintingMapFactory (61.5%)
- **What's covered**: Basic parse with mappings and groups
- **What's missing**: Empty collections, zero-size edge cases
- **Plan**: Gaps are edge cases that don't occur in real ROM data. No new integration test needed — accept or unit test.

#### TrajectoryFactory (55.4%)
- **What's covered**: Basic parse and binary export
- **What's missing**: Terminator handling in code export path
- **Plan**: Gap is in Code export path only. No new integration test needed for binary export.

### Tier 3: Header-only / String Map Files (0%)

These files (`GeoCommand.h`, `BehaviorCommand.h`, `LevelCommand.h`, `SpecialPresetNames.h`, `SurfaceTerrains.h`, `MacroPresets.h`, `geo/GeoUtils.cpp`) contain enum-to-string conversion functions and static maps used only during Code export. They would be covered if we test Code export output, but that's a separate concern from integration testing of the binary pipeline.

## Implementation Priority

### Phase 1: Low-hanging fruit (new configs, low dependency risk)
1. **CollisionFactory complex** — add `test_collision_complex.yml` with a richer collision dataset
2. **MovtexFactory non-quad** — add `test_movtex_nonquad.yml`

### Phase 2: Medium dependency risk
3. **WaterDropletFactory** — add `test_water_droplet.yml` (needs one behavior reference)
4. **GeoLayoutFactory** — add `test_geo_layout.yml` (needs display list references)

### Phase 3: High dependency risk
5. **BehaviorScriptFactory** — add `test_behavior_script.yml` (needs animation/collision references)
6. **LevelScriptFactory** — add `test_level_script.yml` (needs many transitive dependencies)

## Files to create/modify

**New YAML configs** (in `tests/integration/sm64/us/assets/`):
- `test_collision_complex.yml`
- `test_movtex_nonquad.yml`
- `test_water_droplet.yml`
- `test_geo_layout.yml`
- `test_behavior_script.yml`
- `test_level_script.yml`

**Modified**:
- `tests/integration/SM64IntegrationTest.cpp` — add new test cases for each new config

## Verification

```bash
# Build with coverage
cmake -H. -Bbuild-cov -GNinja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_INTEGRATION_TESTS=ON -DENABLE_COVERAGE=ON
cmake --build build-cov -j

# Clear old data, run integration tests, collect coverage
find build-cov -name '*.gcda' -delete
build-cov/torch_integration_tests

# Check SM64 factory coverage specifically
lcov --capture --directory build-cov --output-file coverage.info --ignore-errors mismatch,inconsistent
lcov --extract coverage.info '*/src/factories/sm64/*' --output-file sm64-coverage.info --ignore-errors unused
lcov --list sm64-coverage.info --ignore-errors unused
```

Target: Raise `src/factories/sm64/` line coverage from 12.5% to ~30-40% with Phase 1-2, and ~50%+ with Phase 3.
