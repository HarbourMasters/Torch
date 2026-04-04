# Set_ alternate headers: deferred

## Why Set_ can't be pre-declared in YAML

Set_ alternate headers can't currently be moved out of AddAsset because:

1. **Segment context**: Alternate headers reference segments not in the room's
   YAML `:config:`. The decompressor fails on undeclared segments when processing
   pre-declared Set_ entries during the normal pass.

2. **Decompressor cache dependency**: When created via AddAsset at runtime, the
   parent room's segment data is already cached. Pre-declared entries are processed
   before the cache is warm.

3. **Copyright constraint**: The Set_ binary output contains ROM-extracted data.
   Can't store pre-computed blobs in supplemental JSON.

4. **Serialization complexity**: Generating Set_ binaries in Python would require
   re-implementing ~30 OoT scene command types from OoTSceneFactory.

## Current state

385 Set_ + 1 VTX remain as AddAsset calls. These are fast (cache-warm) and
represent ~1% of the original ~9,000 auto-discovered assets.

35.3s → 8.7s, 35,386/35,386, 386 remaining auto-discoveries.

## Proposed solution: per-asset segment config in YAML

The YAML already has file-level segment config (`:config: segments:`). If Set_
entries could declare their own segments, Torch would configure them before
parsing. The supplemental JSON would include segment info per Set_ entry:

```yaml
bdan_room_11Set_00008C:
  type: OOT:ROOM
  offset: 0x8C
  symbol: bdan_room_11Set_00008C
  base_name: bdan_room_11
  segments:
    - [3, 0x0175E4E0]
    - [2, 0x01734E80]
```

### Tooling change
generate_supplemental.py already reads the ROM and knows the DMA table.
For each Set_ entry, it can include the parent room's segment config
(from the DMA table). The segments are the same as the parent — the Set_
data lives in the same ROM region.

### Torch change
When processing an asset with a `segments` field, Torch temporarily
configures those segments before calling the factory. Small change
in ParseNode or ProcessFile's asset iteration loop.

### Why this works
The Set_ data is in the SAME compressed block as the base room (same segment
3 ROM address). The decompressor just needs to know where to find segment 3.
With per-asset segments, it can decompress and cache the data properly.

### Files to modify
- `soh/tools/generate_supplemental.py` — add segment config per Set_ entry
- `soh/tools/zapd_to_torch.py` — inject segments field in YAML
- `src/Companion.cpp` — read per-asset segments before parsing
