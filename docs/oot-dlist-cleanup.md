# DisplayListFactory cleanup notes

## General approach
- OoT-specific logic should be extractable into helpers/handlers so the
  shared DList code stays close to main
- Replace `GBIMinorVersion::OoT` checks with config-driven gating
  (e.g. `segment_aliasing: true`)
- Open question: gate at call sites or inside helper functions?

## Static helpers

### RemapSegmentedAddr
- Replace GBI version check with config flag
- Function itself is fine, just needs the right gate

### SearchVtx
- Revert shared SearchVtx to original (main) version
- Create OoT-specific SearchVtx that handles OOT:ARRAY and cross-segment comparison
- Only called from one place, dispatch is straightforward

### IsAliasSegment (new)
- Extract alias segment detection loop (duplicated in Export and RemapSegmentedAddr)

## DListFactory::parse

### isAutogen
- Read from YAML but never used — remove

### Removed `segment` variable
- Was unused, removal is fine but causes diff noise

### Child DList symbol derivation
- Derives child DList names from parent naming convention (e.g. "Bmori1_room_0DL_005C98")
- OoT-specific naming pattern — extract to helper and gate
- This is what needs `iomanip` include

### G_RDPHALF_1 auto-discovery (parse)
- Auto-discovers G_BRANCH_Z target DLists — for OoT, AddAsset is skipped so
  the symbol derivation is dead code, only the DeferredVtx scanning runs
- DeferredVtx scanning block uses #ifdef OOT_SUPPORT (because DeferredVtx.h is guarded)
  — extract to a helper in an OoT file so the ifdef isn't inline in shared code
- Symbol derivation duplicated from G_DL handler — should share if kept

### G_MTX auto-discovery (parse)
- Entire block is new (not on main) — added for OoT
- For OoT it just logs a warning (AddAsset skipped)
- Consider removing entirely — debug-only, no other game uses it

### ResolveToAbsolute (new helper)
- Segmented-to-absolute ROM address conversion duplicated in SearchVtx and G_VTX parse
- Extract to shared helper: `uint32_t ResolveToAbsolute(uint32_t addr)`

### G_VTX auto-discovery (parse)
- Uses ResolveToAbsolute inline — should call helper
- "Skip VTX auto-creation" block (skipVtx + alias segment + DeferredVtx + AddAsset skip)
  is entirely OoT deviation — extract to helper, keep original AddAsset path for other games

### Flush per-DList VTX
- Base name extraction + FlushDeferred inline in ifdef block
- Extract to a DeferredVtx helper (e.g. `FlushForDList(node)`) so it's a one-liner

### AddAsset skips for OoT
- Multiple places: child DLists, lights, G_BRANCH_Z targets, G_VTX
- All gated by `GBIMinorVersion::OoT` — wrong concept
- The gate IS needed (performance + avoids duplicating enriched YAML)
- OoT path logs warnings for undeclared assets instead of auto-discovering
- Should gate by config flag (e.g. `skip_autodiscovery`) or check if asset already exists

## DListBinaryExporter::Export

### G_VTX handler
- Extract all OoT-specific VTX handling into a single handler that returns bool —
  if true, the command was handled and the main path is skipped
- Keeps the original main G_VTX flow untouched, diff becomes one added `if` call
- Inside the OoT handler:
  - gSunDL has 3 separate name-based hacks (VTX override, SETTILE/LOADBLOCK format fix,
    and possibly more) — all should be consolidated into one gSunDL handler
  - Alias segment fallback (`w1 + 1` hack) — OTRExporter compat
  - Cross-file VTX check (null vtxDecl) — duplicated in overlap and non-overlap paths, deduplicate
  - OOT:ARRAY type check

### G_DL handler
- Segment 8-13 skip (runtime-swapped, must stay unresolved) — OoT specific
- RemapSegmentedAddr fallback for aliased segments
- Cross-segment DL fallback (`w1 & 0x0FFFFFFF + 1`) — OTRExporter compat
- Same approach: extract OoT handling, keep main path clean

### G_SETTIMG handler
- OTR encoding moved inside `dec.has_value()` — same pattern as lights
- Non-PM64 path now writes `w1 = 0` instead of segment address (verify correctness)
- sShadowMaterialDL hardcode (BSS texture at segment 0xC) — another name-based special case
- Unresolved fallback `(patchedPtr & 0x0FFFFFFF) + 1` — same OTRExporter compat pattern
- Same approach: extract OoT handling

### G_MTX handler
- OOT:MTX → MTX → RemapSegmentedAddr(OOT:MTX) → RemapSegmentedAddr(MTX) fallback chain
- OTR encoding moved inside `dec.has_value()` — same pattern
- Extract OoT type resolution into helper

### G_RDPHALF_1 + G_BRANCH_Z handler
- New handler, converts pair to G_BRANCH_Z_OTR format — standard GBI, not OoT-specific
- G_BRANCH_Z_OTR is handled by LUS (the renderer), not game-specific
- No other game currently uses G_BRANCH_Z, OoT is first to need Torch support
- RemapSegmentedAddr fallback inside is OoT-specific — should use same gating as elsewhere

### G_SETOTHERMODE_H LUT re-encoding
- Un-shifts texture LUT value for sft==14 to match OTRExporter
- INVESTIGATE: LUS interpreter just does `w1 << 32`, no special TEXTLUT handling.
  If we un-shift here, does LUS re-shift correctly? May be SoH-specific behavior.

### G_NOOP zeroing
- OTRExporter always writes {0, 0} for NOOPs via gsDPNoOp()
- ROM NOOPs can carry debug data in lower w0 bits / w1 — we zero it all
- INVESTIGATE: is this because LUS can't handle NOOPs with data, or just cleanup?

### Unhandled opcode zeroing
- Hardcoded set of "handled" opcodes, anything else gets zeroed
- Fragile — tied to what OTRExporter handles
- Comment mentions sCircleDList (OoT-specific)
- Extract into helper, gate it (same config approach as others)
- Investigate if this is needed or if it's papering over a different issue

### Light handling
- Removed "Could not find light" log + commented throw — throw was never active (commented from
  first commit). Restore the log, drop the commented throw
- OTR encoding moved inside `if (res.has_value())` — only write OTR hash when light is resolved
  (was unconditional on main). Probably an intentional fix but verify
