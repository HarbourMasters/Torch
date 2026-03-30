# Plan: Fix 20 Missing Scene-Level DLists

## Context
20 g-prefixed DLists (like `gKinsutaDL_0030B0`, `gMenDL_008118`) are missing from the generated O2R. They were stripped by zapd_to_torch.py's rule "skip all DList entries in room files."

## Key Finding
These g-prefixed DLists are at **different ROM offsets** from the room mesh DLists. They do NOT conflict:
- Mesh DLists: `kinsuta_room_0DL_002BC8` at offset 0x2BC8
- Scene DLists: `gKinsutaDL_0030B0` at offset 0x30B0

The room binary references room-prefixed mesh DLists. The g-prefixed DLists are separate assets that also exist in the O2R under their own names.

## Fix
In `zapd_to_torch.py`, only skip room-prefixed DLists (names starting with the room file name like `kinsuta_room_0`). Keep g-prefixed DLists.

```python
if is_room_file and elem.tag == "DList":
    dlist_name = elem.get("Name", "")
    if not dlist_name or dlist_name.startswith(out_name):
        continue  # Skip room-prefixed DLists (auto-discovered by scene factory)
    # Keep g-prefixed scene-level DLists
```

Then regenerate scene YAMLs: `rm -rf soh/assets/yml/pal_gc/scenes/ && python3 soh/tools/zapd_to_torch.py ...`

## Why Previous Attempt Failed
We tried this exact approach before (commit around the time we first added scene DList skipping) and got 28 failures. At that time, we concluded the g-prefixed DLists conflicted with room DLists. But the investigation shows they're at different offsets. The previous failures may have been from a different issue (possibly the cutscene or pathway bugs that were later fixed).

## Files to Modify
- `soh/tools/zapd_to_torch.py` — change DList skip condition
- Regenerate `soh/assets/yml/pal_gc/scenes/` YAMLs

## Verification
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --failures-only`
- Should show ~20 fewer not-generated, 0 failures
