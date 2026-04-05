# Second round simplification — file-by-file walkthrough (continued)

## Companion.cpp — notes so far

- **Alias system**: ✅ Done — extracted to AliasManager singleton.
- **Experiment (duplicate YAML instead of aliases)**: aliases are fundamentally
  needed because DLists embed CRC64 of own path (bhash). Cannot eliminate.
- **Directory override** (`:config: directory:`): inline in ProcessFile before
  first-pass asset loop. gCurrentDirectory is used to build output paths in
  that loop, so it must be set before it. BUT: gCurrentDirectory is NOT reset
  in the reset block (line 767-778), and NOT re-read in ParseCurrentFileConfig.
  Inconsistent with all other config fields which are reset + re-read.
  **Fix**: add directory read to ParseCurrentFileConfig, add gCurrentDirectory
  reset. Keep the preparse read too (needed for first-pass output paths).
  This makes the two config phases consistent.
- **Extract ProcessFile setup into named methods**: the first 75 lines of
  ProcessFile are setup. Break into:
  - `PreparseConfig(root)` — reads segments + directory from :config
  - `PopulateAddrMap(root)` — first-pass loop populating gAddrMap
  - `ResetTemporalState()` — clears all gCurrent* fields
  Keep YAML reload (`root = YAML::LoadFile`) visible in ProcessFile.
  Makes the flow readable: preparse → populate → reload → reset → full config → parse → export
- **GetNodeByAddr cleanup**: main has a clean version (direct lookup + external files).
  Our version adds two OoT-specific fallback blocks (multi-segment alias scan,
  external VRAM resolution) woven into the shared function.
  **Fix**: clean up both PatchVirtualAddr and GetNodeByAddr:

  **PatchVirtualAddr**:
  - Remove GBIMinorVersion::OoT check (line 1624). The segment 0x80 preference
    is data-driven: only triggers if gVirtualAddrMap has an entry AND segment 0x80
    is in the local map. Non-OoT files won't have either.
  - Extract the VRAM→segmented conversion into a named helper
  - The "addr < vramBase → return addr" early exit handles the segment-0x80-
    that-isn't-VRAM case cleanly already

  **GetNodeByAddr**:
  - Flatten to early-return pattern (our version already does this vs main)
  - Remove GBIMinorVersion::OoT check from multi-segment scan — already gated
    by gVirtualAddrMap presence
  - Extract multi-segment alias scan into helper (e.g. `FindByAbsoluteAddress`)
  - Extract external VRAM resolution into helper (e.g. `FindInExternalVRAM`)
  - Main function becomes: direct lookup → alias fallback → external lookup →
    external VRAM fallback → nullopt

  All OoT-specific behavior is gated by gVirtualAddrMap / segment.local data,
  not by GBI version. No config.yml changes needed — existing YAML `:config: virtual:`
  already drives the behavior.
