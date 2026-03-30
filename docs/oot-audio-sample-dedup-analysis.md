# Audio Sample Dedup/Naming Analysis

## Problem
3 samples fail: "Tom Drum" (banks 1,5,6), "Drum Sidestick" (banks 1,3), "Windchimes" (banks 1,6). These names appear in multiple banks in the XML/YAML with different per-bank offsets.

## Root Cause

### How dedup works
- Samples are deduplicated by **absolute Audiotable offset** (`dataRelPtr + sampleBankTable[bankId].ptr`)
- If two banks reference the same sample data at the same absolute offset, only one entry is created
- If two banks reference the same-named sample at DIFFERENT absolute offsets, BOTH entries are created

### How naming works
- Name resolution: `sampleNames[bankId][relativeOffset]` → sample name from YAML
- Registration: `RegisterCompanionFile("samples/" + name + "_META", data)`
- When two entries get the same name, the second **overwrites** the first

### The collision
1. Font using bank 1 discovers "Tom Drum" at absolute offset A → writes `samples/Tom Drum_META`
2. Font using bank 5 discovers "Tom Drum" at absolute offset B (A ≠ B) → overwrites with different data
3. Reference expects the bank 1 version; we end up with the bank 5 version (or vice versa)

## How ZAPDTR handles this

In `ZAudio.cpp ParseSampleEntry` (line ~128):
```cpp
if (samples.find(sampleDataOffset) == samples.end()) {
    // Only create if this absolute offset hasn't been seen
    sample->fileName = sampleOffsets[bankIndex][sampleDataOffset];
    samples[sampleDataOffset] = sample;
}
```

The `sampleOffsets` map is populated from XML:
```cpp
sampleOffsets[bankId][atOffset] = name;
```

where `atOffset` is the XML Offset attribute value.

**Key question**: Is the XML offset the relative offset (within bank) or the absolute Audiotable offset? From the XML:
- Bank 1 Tom Drum: Offset="0x335740" — this is very large, looks like absolute Audiotable offset
- Bank 5 Tom Drum: Offset="0x7B70" — small, looks like relative within bank

If bank 1's offset is already absolute and matches the `sampleDataOffset`, the name lookup succeeds directly. If bank 5's offset is relative, `sampleDataOffset` = 0x7B70 + sampleBankTable[5].ptr, which might equal a different absolute offset.

**Critical**: In ZAPDTR's `ParseSampleEntry`, the name lookup is:
```cpp
sample->fileName = sampleOffsets[bankIndex][sampleDataOffset];
```

The key `sampleDataOffset` is the ABSOLUTE offset. But the XML stores a different kind of offset per bank. This means:
- For bank 1: XML offset 0x335740 is used as the key. If `sampleDataOffset` also = 0x335740, name resolves
- For bank 5: XML offset 0x7B70. If `sampleDataOffset` = 0x7B70 + bankBase, name won't resolve (different value)

So ZAPDTR likely ONLY resolves names for bank 1 (where XML offsets = absolute offsets), and bank 5/6 samples either:
1. Get fallback names (`sample_5_XXXXX`)
2. Or their absolute offsets happen to match bank 1's (meaning they share physical data)

## Possible Fixes for Torch

### Option A: Precompute absolute offsets for name resolution
Convert all YAML per-bank relative offsets to absolute offsets before name lookup:
```python
absoluteOffset = yamlOffset + sampleBankTable[bank].ptr
```
Then key the name map by absolute offset.

### Option B: First-bank-wins dedup on name
If a companion file with the same name was already registered, skip it.
Track registered names in a set and check before `RegisterCompanionFile`.

### Option C: Check if sample data is identical
If two banks reference different absolute offsets but the sample data is byte-identical,
only write one entry (the first discovered). This handles the case where banks share
physical data at different offsets.

## Next Steps
1. Check the reference O2R — does it have ONE "Tom Drum_META" or multiple?
2. If one, which bank's version is it?
3. Implement the appropriate dedup strategy
