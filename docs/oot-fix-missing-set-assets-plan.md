# Fix 3,099 missing assets in enriched YAML run

## Context
After YAML enrichment, O2R generation runs in 8.1s (down from 35.3s) but produces
32,287/35,386 assets. 3,099 assets are "not generated" — all scene Set_ variants
and downstream effects (ActorEntry, PathwayList).

## Missing asset breakdown
- 468 Set_ scene/room headers (OOT:ROOM/OOT:SCENE at unique offsets)
- 2,388 Set_ DLists (aliases — same binary data as base DLists)
- 243 Set_ ActorEntry/PathwayList companion files

## Root cause
`catalog_undeclared.py` skips ALL `Set_` assets. Set_ entries are alternate scene
headers (child night, adult day, etc.) that are created by AddAsset at unique offsets
(the alternate header table offset, NOT the room data offset). When Set_ rooms aren't
pre-declared, OoTSceneFactory's alternate header processing (line 860-883) calls
AddAsset which throws, so the Set_ room never gets parsed and its DList aliases
and companion files never get created.

## How Set_ processing works (OoTSceneFactory.cpp)
1. Base room parses SetAlternateHeaders command → reads alt header pointer table
2. Each non-zero pointer becomes a pending alt header with symbol like `bdan_room_0Set_0000E0`
3. After primary commands, processes pending alt headers:
   a. Checks `GetNodeByAddr(alt.seg)` — if exists, skips (already processed)
   b. If not: calls `AddAsset({type: OOT:ROOM, offset: alt.seg, symbol: ...})`
   c. AddAsset → RegisterAsset → ParseNode → OoTSceneFactory.parse for Set_ room
   d. Set_ room parse walks SetMesh → ResolveGfxWithAlias → registers DList aliases
   e. Set_ room parse walks SetActorList/SetPathways → creates companion files

## Fix

### Step 1: catalog_undeclared.py — keep Set_ room/scene entries
Change Set_ skip logic to only skip Set_ GFX/MTX/OOT:ARRAY (aliases).
Keep Set_ OOT:ROOM and OOT:SCENE (they have unique offsets and need processing).

Move the Set_ skip from `parse_o2r_path()` into the main loop, after reading the
resource type. Skip if Set_ AND type is GFX/MTX/OOT:ARRAY. Allow if Set_ AND type
is OOT:ROOM/OOT:SCENE/OOT:COLLISION/etc.

### Step 2: Handle Set_ room offset extraction
Set_ room names like `bdan_room_0Set_0000E0` have offset `0x0000E0` in the `Set_`
suffix. The current offset regex `_([0-9A-Fa-f]{4,8})$` matches `0000E0` correctly.
ROOM_PATTERN `^(.+?(?:_room_\d+|_scene))(.*)` matches `bdan_room_0` correctly,
mapping the Set_ entry to the right YAML file.

### Step 3: Modify OoTSceneFactory alternate header logic
Pre-declaring Set_ rooms makes the alternate header guard (line 861: `if (!existing)`)
skip them. But they still need to be parsed for alias/companion creation.

Option (b) — simplest for PoC: when the Set_ entry exists in gAddrMap, still parse
it so aliases get registered:
```cpp
if (!existing.has_value()) {
    Companion::Instance->AddAsset(altNode);
} else {
    // Asset pre-declared — still need to parse for alias/companion creation
    std::string output = currentDir + "/" + alt.symbol;
    auto node = std::get<1>(existing.value());
    node["base_name"] = entryName;
    Companion::Instance->ParseNode(node, output);
}
```

## Files to modify
- `soh/tools/catalog_undeclared.py` — keep Set_ OOT:ROOM/OOT:SCENE
- `src/factories/oot/OoTSceneFactory.cpp` — process pre-declared Set_ entries (line ~861)

## Verification
1. `rm -rf soh/assets/yml/pal_gc/ && python3 soh/tools/zapd_to_torch.py ...`
2. `python3 soh/tools/catalog_undeclared.py ... && inject`
3. `python3 soh/tools/test_assets.py` → 35,386/35,386
4. Timing ~8s
