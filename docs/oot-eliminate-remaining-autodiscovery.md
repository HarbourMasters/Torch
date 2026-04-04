# Eliminate remaining 681 auto-discovered assets

## Context
After YAML enrichment, 8,778 assets are pre-declared, reducing O2R generation from
35.3s to 9.3s with 35,386/35,386 pass. But 681 assets still go through AddAsset:

| Type | Count | Blocker |
|------|-------|---------|
| Set_ OOT:ROOM | 251 | Needs base_name from parent room parse |
| BLOB (SkelLimbs) | 194 | 0-byte companion files, no offset in name |
| Set_ OOT:SCENE | 134 | Needs base_name from parent scene parse |
| MTX | 101 | Offset is G_MTX w1 operand, not in O2R name |
| VTX | 1 | gSunDL special case, no standalone asset in O2R |

## Approach by category

### BLOB limb tables (194) — catalog 0-byte files from O2R
These are 0-byte companion files (no resource header). catalog_undeclared.py skips
files < 64 bytes. Fix: also catalog 0-byte files by name pattern.

The offset comes from the skeleton that references the limb table. We can't get it
from the BLOB itself (it's empty). But we don't need the offset in YAML — when the
skeleton factory calls ResolvePointer for the limb table address, it finds it in
gAddrMap if pre-declared. The key is matching the right YAML file and providing a
valid offset.

Actually: the BLOB is registered at the limbsArrayAddr (a segmented address like
0x06006000). The file-relative offset is 0x6000. The O2R name is just
`gArrowSkelLimbs` with no offset suffix. We need to read the offset from the
skeleton's YAML entry or from the O2R's skeleton binary data.

**Simplest approach**: read each skeleton's binary from the O2R, extract the
limbsArrayAddr pointer, and generate BLOB entries with the correct offset.

### MTX (101) — extract offset from parent DList binary
MTX names like `bdan_room_10DL_000C68Mtx_000000` encode the parent DList offset
(000C68) but the MTX's own offset is the G_MTX w1 operand. We'd need to walk
each parent DList's GBI commands to find the G_MTX instruction and extract w1.

**Simpler approach**: the O2R already has these MTX assets. We just can't derive
the offset from the name. But we CAN store the offset in the JSON by reading
it from the skeleton or DList binary in the O2R.

Actually, MTX binary data in the O2R is just a 64-byte matrix. The offset
is what Torch uses for gAddrMap lookup. Without the correct offset, the MTX
won't be found by GetNodeByAddr. So we need the real segmented offset.

**Pragmatic approach**: for the PoC, accept 101 MTX auto-discoveries. They
early-return quickly in AddAsset (found via GetNodeByAddr after first creation).
Each MTX AddAsset is cheap — just creates a 128-byte resource.

### Set_ OOT:ROOM/OOT:SCENE (385) — accept auto-discovery
These need base_name context from the parent parse. Pre-declaring them in YAML
without base_name causes incorrect sub-asset naming (pathways, backgrounds get
Set_-prefixed names). Fixing this requires either:
- Setting base_name in YAML (requires knowing parent from O2R structure)
- Changing OoTSceneFactory to derive base_name differently

For the PoC, accept these as auto-discovered. They go through AddAsset but
early-return if already registered. The first invocation creates them.

### VTX (1) — accept
gSunDL special case. No standalone VTX in reference O2R. Accept.

## Implementation — eliminate all 681

Goal: zero AddAsset calls for OoT. Enables removing AddAsset dependency from
OoTSkeletonFactory, DisplayListFactory, and OoTSceneFactory.

### 1. BLOB limb tables (194)
**Tooling change**: catalog_undeclared.py
- Scan O2R for 0-byte files matching `*SkelLimbs` pattern (no resource header)
- For each, find the parent skeleton in the same O2R directory
- Read skeleton binary: offset 0x40 has pointer to limbs array → that's the BLOB offset
- Generate BLOB entry: type=BLOB, offset=(extracted), symbol=name, size=0

### 2. MTX (101)
**Tooling change**: catalog_undeclared.py
- These are scene MTX assets named `{roomDL}Mtx_000000`
- The real offset is the G_MTX w1 operand inside the parent DList binary
- Read parent DList from O2R, scan for G_MTX opcodes, extract w1 as the offset
- Generate MTX entry: type=MTX, offset=(w1), symbol=name

### 3. Set_ OOT:ROOM/OOT:SCENE (385)
**Tooling + Torch change**:
- catalog_undeclared.py: stop skipping Set_ OOT:ROOM/OOT:SCENE entries
- Derive base_name from the Set_ name: `bdan_room_0Set_0000E0` → base_name=`bdan_room_0`
- Write base_name field in YAML injection
- Torch: OoTSceneFactory alternate header processing — when Set_ entry exists in
  gAddrMap (pre-declared with base_name), don't call AddAsset but still need to
  trigger the scene parse for aliases/companions
- This is the approach we tried before (ParseNode for existing entries). The 31
  failures were from incorrect naming. With base_name properly set in YAML, the
  factory reads it correctly and sub-assets get canonical names.

### 4. VTX — gSunDL (1)
**Torch change**: DisplayListFactory gSunDL handler
- The gSunDL override at line 321 already handles this VTX differently (ranged match)
- The AddAsset call for this VTX is in the general G_VTX handler BEFORE the gSunDL
  override fires. Need to check: does the VTX AddAsset actually affect the output?
- If the AddAsset fails/throws and gSunDL still works correctly → just suppress it
- If not: add the VTX to YAML manually (single asset, hardcoded)

## Files to modify
- `soh/tools/catalog_undeclared.py` — BLOB scanning, MTX offset extraction, Set_ entries
- `soh/tools/zapd_to_torch.py` — base_name field injection
- `src/factories/oot/OoTSceneFactory.cpp` — ParseNode for pre-declared Set_ entries
- `src/Companion.h` — ParseNode public (for OoTSceneFactory access)
- `src/Companion.cpp` — AddAsset throw list update

## Verification
1. Regen, catalog, inject
2. AddAsset throw for ALL types (no allow list)
3. `test_assets.py` → 35,386/35,386
4. Timing ~9s
