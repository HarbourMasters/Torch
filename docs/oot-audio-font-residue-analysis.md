# Fix 2 remaining font failures: drum→instrument stack residue

## Context
2 fonts (10_Fire_Temple, 35_Game_Over) fail because invalid instruments before any valid
instrument have non-zero residue bytes. These come from ZAPDTR's stack reuse: the compiler
places `DrumEntry drum` and `InstrumentEntry instrument` at the same stack location since
their scopes don't overlap. The behavior is **deterministic** — same ROM always produces
same output.

## Root Cause (verified)
In ZAPDTR's `ParseSoundFont` (ZAudio.cpp:215-305):
1. Drum loop: `DrumEntry drum = {0};` then overwrites fields with ROM data
2. Instrument loop: `InstrumentEntry instrument;` — POD fields uninitialized
3. Compiler reuses same stack slot → instrument POD fields contain drum residue

### Field mapping (DrumEntry → InstrumentEntry at same stack address):
| DrumEntry field   | Offset | InstrumentEntry field | Value for last drum |
|-------------------|--------|-----------------------|---------------------|
| releaseRate       | 0      | isValidInstrument     | (we set explicitly) |
| **pan**           | 1      | **loaded**            | last drum's pan     |
| **loaded**        | 2      | **normalRangeLo**     | last drum's loaded  |
| (padding)         | 3      | **normalRangeHi**     | 0                   |
| offset (low byte) | 4      | **releaseRate**       | 0                   |

### Evidence:
- Font 10: drums all have pan=64(0x40) → invalid inst loaded=0x40 ✓
- Font 35: drums all have pan=74(0x4A) → invalid inst loaded=0x4A ✓
- Both: normalRangeHi=0, releaseRate=0 (from drum padding/offset=0) ✓

## Fix
In `OoTAudioFactory.cpp`, after the drum loop, seed the instrument residue from the
last drum's fields using the mapping above:

```cpp
// After drum loop:
if (!drums.empty()) {
    auto& [rr, pan, loaded, tuning, env, ref] = drums.back();
    // DrumEntry→InstrumentEntry stack mapping
    lastLoaded = pan;           // drum.pan (offset 1) → inst.loaded (offset 1)
    lastNormalRangeLo = loaded;  // drum.loaded (offset 2) → inst.normalRangeLo (offset 2)
    lastNormalRangeHi = 0;       // padding (offset 3) → inst.normalRangeHi (offset 3)
    lastReleaseRate = 0;         // drum.offset=0 (offset 4) → inst.releaseRate (offset 4)
}
```

## Files to modify
- `src/factories/oot/OoTAudioFactory.cpp` — add drum residue seeding after drum loop

## Verification
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --category audio`
- Expected: 598/598 pass (was 596/598)
