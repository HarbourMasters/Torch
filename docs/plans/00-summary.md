# Plan Documents Summary

Overview of all planning documents for the `unit-testing-discovery` branch.

## Discovery & Strategy

- **[01-unit-testing-discovery](01-unit-testing-discovery.md)** — Initial codebase analysis identifying 79 factories, 21 testing categories, and 323 proposed tests across priority tiers P0–P4.

## Unit Test Implementation

- **[02-unit-testing-implementation-plan](02-unit-testing-implementation-plan.md)** — P0 (foundation): Google Test setup, BinaryReader/Writer, StringHelper, TorchUtils, Decompressor.
- **[03-p1-testing-implementation-plan](03-p1-testing-implementation-plan.md)** — P1 (data types): Vec3D, TextureUtils, VtxFactory, DisplayListFactory.
- **[04-p2-testing-implementation-plan](04-p2-testing-implementation-plan.md)** — P2 (simple + SM64 factories): Viewport, Lights, Float, Mtx, SM64 collision/geo/behavior structures, Companion registration.
- **[05-p3-testing-implementation-plan](05-p3-testing-implementation-plan.md)** — P3 (utilities + formats): CRC64 hashing, n64graphics pixel conversions, NAudio, SF64 types.
- **[06-p4-testing-implementation-plan](06-p4-testing-implementation-plan.md)** — P4 (game-specific): MK64, F-Zero X, PM64, SM64 animation data.

## Integration Testing

- **[07-integration-testing-plan](07-integration-testing-plan.md)** — Pipeline architecture for end-to-end ROM-based tests. SM64 US as first target with header validation.
- **[08-integration-testing-additional-factories-plan](08-integration-testing-additional-factories-plan.md)** — Expanded SM64 integration tests: animations, dialog, text, geo layouts, compressed segments, dictionary.

## Review & Remediation

- **[09-test-review](09-test-review.md)** — Audit of all 31 test files. Identifies high/low value tests, 2 critical issues, and struct-only tests that should test parse logic.
- **[10-test-review-remediation-plan](10-test-review-remediation-plan.md)** — Fixes for review findings: fuzzy matching, duplicates, and upgrading 7 struct-only tests to BinaryReader parse tests.

## Coverage & Gaps

- **[11-coverage-reporting-plan](11-coverage-reporting-plan.md)** — `ENABLE_COVERAGE` CMake option, lcov/genhtml integration, CI artifact upload.
- **[12-sm64-integration-coverage-gaps](12-sm64-integration-coverage-gaps.md)** — lcov-driven gap analysis of SM64 factories. Prioritized plan for new integration test configs.
- **[13-future-improvements](13-future-improvements.md)** — Next steps: uncovered geo opcodes, 0%-coverage factories, code/header export paths, infrastructure ideas.
