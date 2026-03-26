# Future Improvements

Ideas for continuing work on testing and coverage from the `unit-testing-discovery` branch.

## GeoLayout Coverage

- **More actor geos**: The bomb and goomba tests added CullingRadius, Scale, Billboard, Shadow, AnimatedPart, SwitchCase, and Branch. Still uncovered: NodeRoot, NodePerspective, NodeCamera, NodeBackground, NodeOrthoProjection, NodeTranslationRotation, NodeTranslation, NodeRotation, BranchAndLink, Return, NodeLevelOfDetail, NodeHeldObj, NodeMasterList, CopyView, AssignAsView, UpdateNodeFlags.
- **Level geos**: These would cover NodeRoot/Perspective/Camera/Background but require `external_files` due to mixed compression (uncompressed geo segment + MIO0 data segment). Need to pre-declare all GFX/VTX dependencies in the data file so auto-discovery doesn't hang on cross-segment MIO0 access.
- **Complex actors with external_files**: Bowser's geo hangs because its GFX references segments with different compression. Could work if we set up proper external_files with pre-declared data assets, similar to how Ghostship configs are structured.

## Collision Coverage

- CollisionFactory is at 50%. The uncovered paths are mostly special objects (TERRAIN_LOAD_OBJECTS) and the high surface type range (TERRAIN_LOAD_IS_SURFACE_TYPE_HIGH). Finding a level collision that exercises these would help.
- The Code exporter is entirely uncovered since integration tests only exercise binary export.

## Factories at 0% Coverage

- **BehaviorScriptFactory**: Uses VRAM addresses (0x8033xxxx), not segment-mapped. No Ghostship US configs exist. Would need a custom approach or VRAM mapping setup.
- **LevelScriptFactory**: Complex dependency chains. Level scripts reference many other assets. No Ghostship US configs.
- **WaterDropletFactory**: No Ghostship US configs. The AddAsset call for behavior scripts is commented out in the source.

## Code/Header Export Paths

- Many factories (Macro, MovtexQuad, Painting, PaintingMap, Trajectory) have ~50% coverage because only the binary exporter runs. The Code and Header exporters are untested. Would need a way to trigger non-binary export in integration tests, or dedicated unit tests for the export functions.

## Test Infrastructure

- Consider adding a helper to run the pipeline with a specific export mode (Code vs Binary) so we can test Code exporters.
- The `external_files` pattern for mixed-compression segments is well-understood now — document it as a testing pattern.
- Coverage reporting in CI uploads artifacts but doesn't enforce thresholds or track trends. Could add a coverage summary comment on PRs or a minimum threshold gate.

## Ghostship Offset Delta

- Ghostship YAML offsets for some segments (e.g., segment 0x6 bomb data) have a consistent 0x150 delta from the segment virtual addresses. This doesn't affect auto-discovery (which uses binary addresses directly) but means we can't blindly copy Ghostship offsets for pre-declared assets. The mist (segment 0x3) has no delta. Worth understanding why if we need to pre-declare many assets.
