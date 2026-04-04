# Consolidate supplemental JSON for YAML generation

## Context

YAML generation currently requires 3+ JSON inputs beyond the XML and DMA:
- `vtx.json` (from extract_vtx.py + reference O2R) — checked into repo
- `undeclared.json` (from catalog_undeclared.py + reference O2R + YAML) — generated per-run
- `rom_assets.json` (from extract_rom_assets.py + ROM) — WIP, generated per-run

Goal: One `supplemental.json` per ROM version, checked into the repo, containing
ALL asset data the XML doesn't provide. zapd_to_torch takes XML + DMA + supplemental
→ fully enriched YAML. No per-run extraction steps.

## Design

### Single script: generate_supplemental.py
Combines extract_vtx.py + catalog_undeclared.py + extract_rom_assets.py into one
script that produces supplemental.json.

**Inputs**: reference O2R + ROM + DMA JSON
**Output**: `soh/supplemental/<version>.json`

The supplemental JSON contains every asset that needs to be in YAML but isn't in
the ZAPD XML. Organized by file_key → list of asset entries:

```json
{
  "objects/gameplay_keep": [
    {"name": "...", "type": "GFX", "offset": "0x...", "symbol": "..."},
    {"name": "...", "type": "OOT:ARRAY", "offset": "0x...", "count": 32, "array_type": "VTX", "symbol": "..."},
    {"name": "...", "type": "OOT:LIMB", "offset": "0x...", "limb_type": "Standard", "symbol": "..."},
    {"name": "...", "type": "BLOB", "offset": "0x...", "size": 0, "symbol": "..."},
    {"name": "...", "type": "MTX", "offset": "0x...", "symbol": "..."}
  ],
  "scenes/nonmq/bdan_room_0": [
    {"name": "...", "type": "OOT:ROOM", "offset": "0x...", "symbol": "...", "base_name": "bdan_room_0"}
  ]
}
```

### zapd_to_torch.py simplification
Replace `--vtx-json`, `--undeclared-json`, `--rom-assets-json` with single
`--supplemental-json`. Keep old flags for backward compatibility but deprecate.

Pipeline becomes:
```bash
python3 zapd_to_torch.py \
  --xml-dir ~/code/Shipwright/soh/assets/xml/GC_NMQ_PAL_F \
  --dma-json soh/dma/pal_gc.json \
  --out-dir soh/assets/yml/pal_gc \
  --supplemental-json soh/supplemental/pal_gc.json
```

### generate_supplemental.py flow
1. Read reference O2R → extract all asset metadata (types, offsets, counts, limb_types)
2. Read ROM + DMA → decompress Yaz0 scenes, extract Set_ headers + MTX offsets
3. Merge into single JSON
4. For assets derivable from both sources, prefer ROM data (more authoritative)

Over time, more extraction moves from "read O2R" to "read ROM", eventually
eliminating the O2R dependency entirely.

### Scene shared/nonmq mapping
DMA names don't encode shared vs nonmq. Options:
a. Hardcode a mapping table in the script (simple, fragile)
b. Read from the XML directory structure (XML has GC_NMQ_PAL_F which encodes NMQ)
c. Accept DMA names as keys and have zapd_to_torch map them (it already knows
   the directory structure from XML processing)

Option (c) is cleanest: supplemental.json uses DMA names as keys (e.g. "bdan_room_0"),
and zapd_to_torch maps them to the correct YAML path during injection (it already
knows which files it created from the XML step).

### What goes in supplemental.json

| Type | Count | Source | Currently |
|------|-------|--------|-----------|
| OOT:ARRAY (VTX) | ~3,659 | O2R (extract_vtx) | vtx.json |
| GFX (child DLists) | ~3,937 | O2R (catalog_undeclared) | undeclared.json |
| OOT:ARRAY (non-VTX) | ~3,318 | O2R (catalog_undeclared) | undeclared.json |
| OOT:LIMB | ~1,321 | O2R (catalog_undeclared) | undeclared.json |
| BLOB (limb tables) | ~194 | O2R skeleton binary | undeclared.json |
| OOT:COLLISION | ~101 | O2R (catalog_undeclared) | undeclared.json |
| MTX | ~101 | ROM DList walk | rom_assets.json |
| Set_ OOT:ROOM | ~251 | ROM scene headers | rom_assets.json |
| Set_ OOT:SCENE | ~134 | ROM scene headers | rom_assets.json |
| VTX (gSunDL) | 1 | hardcoded | not yet handled |

### Checked-in files
- `soh/supplemental/pal_gc.json` — generated once, checked in
- `soh/supplemental/pal_mq.json` — etc for each ROM version
- `soh/supplemental/README.md` — documents format and generation

### Migration
1. Create generate_supplemental.py merging all 3 existing scripts
2. Generate supplemental.json for pal_gc, verify against existing vtx.json + undeclared.json
3. Add --supplemental-json to zapd_to_torch.py
4. Test: regen YAML from supplemental, run test_assets → 35,386/35,386
5. Generate supplemental for all 11 ROM variants
6. Remove old --vtx-json, --undeclared-json, --rom-assets-json flags
7. Delete extract_vtx.py, catalog_undeclared.py, extract_rom_assets.py (merged)
8. Delete soh/vtx/ directory (data now in soh/supplemental/)

## Files to create/modify
- `soh/tools/generate_supplemental.py` — new, merges existing extraction scripts
- `soh/tools/zapd_to_torch.py` — add --supplemental-json, deprecate old flags
- `soh/supplemental/*.json` — new, one per ROM version
- Delete: `soh/tools/extract_vtx.py`, `soh/tools/catalog_undeclared.py`, `soh/tools/extract_rom_assets.py`
- Delete: `soh/vtx/` directory

## Verification
1. `python3 generate_supplemental.py <rom> <dma.json> <reference.o2r> soh/supplemental/pal_gc.json`
2. `python3 zapd_to_torch.py --xml-dir ... --dma-json ... --out-dir ... --supplemental-json soh/supplemental/pal_gc.json`
3. `python3 test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
4. Timing: ~8-9s
