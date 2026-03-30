# Plan: Fix 20 Missing Scene-Level DLists (v3)

## Context
20 g-prefixed DLists missing from O2R. Adding them to YAML before OOT:ROOM entries causes 838 regressions because GFX factory's VTX auto-discovery pollutes gAddrMap before scene factory runs.

## Root Cause
YAML entries are processed in order. When a GFX DList is processed BEFORE the OOT:ROOM entry:
1. GFX factory parses the DList, auto-discovers VTX, registers them in gAddrMap
2. Scene factory later processes the room, calls SetMesh, auto-discovers its own DLists
3. Scene's DList parsing finds pre-registered VTX in gAddrMap from step 1
4. VTX overlap detection and merge logic breaks — produces wrong VTX boundaries

## Fix: Reorder YAML entries so GFX comes AFTER OOT:ROOM

In `zapd_to_torch.py`, when writing room YAML files that contain both scene-level DLists and Room entries, ensure the Room entry is written FIRST and DList entries come AFTER.

The scene factory processes the Room entry, which handles all SetMesh DList auto-discovery with clean VTX state. Then the GFX factory processes the g-prefixed DLists, which now find the already-registered mesh VTX without conflict (since the scene factory has already finalized its VTX state).

### Implementation in zapd_to_torch.py

In the `write_yaml` function, sort assets so OOT:ROOM/OOT:SCENE entries come before GFX entries:

```python
# Sort assets: scene/room types first, then everything else
def asset_sort_key(asset):
    t = asset.get("type", "")
    if t in ("OOT:SCENE", "OOT:ROOM"):
        return 0  # First
    return 1  # After

assets.sort(key=asset_sort_key)
```

Then re-enable the g-prefixed DLists by changing the skip condition:
```python
if is_room_file and elem.tag == "DList":
    dlist_name = elem.get("Name", "")
    if not dlist_name or dlist_name.startswith(out_name):
        continue  # Skip room-prefixed (auto-discovered by scene factory)
    # Keep g-prefixed scene-level DLists
```

### Files to Modify
1. `soh/tools/zapd_to_torch.py` — reorder assets in write_yaml, change DList skip condition
2. Regenerate scene YAMLs

### Verification
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --failures-only`
- Should show ~20 fewer not-generated, 0 failures, 0 regressions
