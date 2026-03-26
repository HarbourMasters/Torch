# Plan: Add Code Coverage Reporting

## Context

The test suite on `unit-testing-discovery` now has ~400 test cases across 31 unit test files and 15 integration tests. We need visibility into which production code paths these tests actually exercise. Coverage reporting should work both in CI (automated, every push) and locally (developer-driven).

## Approach

### 1. CMake: Add `ENABLE_COVERAGE` option

**File: `CMakeLists.txt`**

Add a new option following the existing `ENABLE_ASAN` pattern:

```cmake
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(ENABLE_COVERAGE)
    add_compile_options(--coverage -fno-inline)
    add_link_options(--coverage)
endif()
```

Place this right after the `ENABLE_ASAN` block (after line 121). The `--coverage` flag is shorthand for `-fprofile-arcs -ftest-coverage` and works with both GCC and Clang. `-fno-inline` improves line-level accuracy.

### 2. CI: Add coverage collection and upload

**File: `.github/workflows/tests.yml`**

Changes to the existing workflow:

- Add `lcov` to the `Install dependencies` step
- Add `-DENABLE_COVERAGE=ON` to the Configure step
- Add a new step after tests run to collect coverage with `lcov` and generate HTML with `genhtml`
- Upload the HTML report as a GitHub Actions artifact

```yaml
- name: Install dependencies
  run: sudo apt-get install cmake ninja-build libbz2-dev lcov

- name: Configure
  run: cmake -H. -Bbuild -GNinja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_INTEGRATION_TESTS=ON -DENABLE_COVERAGE=ON

# ... existing Build, Unit Tests, Integration Tests steps unchanged ...

- name: Collect Coverage
  run: |
    lcov --capture --directory build --output-file coverage.info --ignore-errors mismatch,inconsistent
    lcov --remove coverage.info '/usr/*' '*/lib/*' '*/build/_deps/*' '*/tests/*' --output-file coverage.info --ignore-errors unused
    genhtml coverage.info --output-directory coverage-report --ignore-errors inconsistent

- name: Upload Coverage Report
  uses: actions/upload-artifact@v4
  with:
    name: coverage-report
    path: coverage-report/
```

Key details:
- `lcov --remove` filters out system headers, third-party libs (`lib/`, `_deps/`), and test code itself — we only want coverage of `src/`
- `--ignore-errors mismatch,inconsistent` handles gcov/lcov version mismatches (lcov 2.0+ is stricter about gcov output consistency)
- The report is uploaded as an artifact, downloadable from the Actions tab

### 3. Local developer workflow

No new scripts or Makefile targets needed — just documented CMake usage:

```bash
# Configure with coverage
cmake -H. -Bbuild-cov -GNinja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON

# Build and run tests
cmake --build build-cov -j
build-cov/torch_tests

# Collect and view report
lcov --capture --directory build-cov --output-file coverage.info --ignore-errors mismatch,inconsistent
lcov --remove coverage.info '/usr/*' '*/lib/*' '*/build-cov/_deps/*' '*/tests/*' --output-file coverage.info --ignore-errors unused
genhtml coverage.info --output-directory coverage-report --ignore-errors inconsistent
# Open coverage-report/index.html in browser
```

Using a separate `build-cov` directory avoids polluting the normal build with `.gcno`/`.gcda` files.

## Files to modify

1. **`CMakeLists.txt`** — Add `ENABLE_COVERAGE` option + compile/link flags
2. **`.github/workflows/tests.yml`** — Add lcov install, coverage flag, collection step, artifact upload
3. **`docs/plans/11-coverage-reporting-plan.md`** — This plan document

## Verification

```bash
# Local: build with coverage and verify .gcno files are created
cmake -H. -Bbuild-cov -GNinja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON
cmake --build build-cov -j
ls build-cov/CMakeFiles/torch_tests.dir/src/**/*.gcno  # should exist

# Run tests and collect coverage
build-cov/torch_tests
lcov --capture --directory build-cov --output-file coverage.info --ignore-errors mismatch,inconsistent
lcov --remove coverage.info '/usr/*' '*/lib/*' '*/build-cov/_deps/*' '*/tests/*' --output-file coverage.info --ignore-errors unused
genhtml coverage.info --output-directory coverage-report --ignore-errors inconsistent
# Open coverage-report/index.html — verify src/ files show line coverage
```
