# VTX YAML entries and reference O2R dependency

## Background
`zapd_to_torch.py` converts ZAPDTR XML asset definitions to Torch YAML format. The XML
files declare DLists, textures, skeletons, etc. — but NOT vertex arrays (VTX). In ZAPDTR,
VTX assets are discovered dynamically during DList processing and named by convention
(e.g. `gameplay_dangeon_keepVtx_000400`).

## Why we need the reference O2R
The YAML generator's "Step 2" backfills VTX entries by scanning the reference O2R manifest
for VTX asset paths and adding matching YAML entries. This ensures Torch produces VTX assets
with the same names that SoH's game code expects at runtime.

Without the backfill, Torch's DeferredVtx system still creates VTX assets during DList
processing, but with different names (e.g. `gameplay_dangeon_keep_seg5_vtx_400` instead of
`gameplay_dangeon_keepVtx_000400`). It also splits/merges VTX ranges differently, producing
11,760 entries vs the expected 3,659.

## Investigation results (2026-03-30)
Ran Torch with YAMLs generated without VTX backfill:
- Reference O2R: 35,386 files
- Generated O2R: 43,487 files
- 3,659 VTX missing (wrong names)
- 11,760 extra VTX (DeferredVtx auto-naming)

## Alternatives considered

### 1. Remove backfill entirely
Doesn't work — VTX naming mismatch means SoH can't find assets at runtime.

### 2. Generate VTX entries from ROM data
Parse raw GBI commands in DList ROM data to discover VTX references statically. Would
require the ROM during YAML generation (not just during Torch execution). Duplicates
DList parsing logic already in Torch's C++ code and needs to stay in sync with it.

### 3. Fix DeferredVtx naming to match ZAPDTR
Would need DeferredVtx to produce names matching ZAPDTR convention. Non-trivial since
ZAPDTR naming depends on the XML file name and offset format, and DeferredVtx doesn't
have access to that context.

## Current status
The reference O2R dependency is acceptable for now. The YAMLs will be checked in to the
repo for the SoH migration, and the reference O2R is only needed when regenerating them.
