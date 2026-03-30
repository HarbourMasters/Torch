# Audio Sample Dedup/Naming Analysis (Verified)

## Problem
Generated O2R has 441 samples (all named), reference has 449 (430 named + 19 fallback).
3 named samples have wrong data due to cross-bank collisions: "Tom Drum" (banks 1,5,6),
"Drum Sidestick" (banks 1,3), "Windchimes" (banks 1,6). 8 total entries lost to overwrites.

## Root Cause (verified against ZAPDTR source)

### How ZAPDTR handles sample naming

**XML parsing** (`ZAudio.cpp:54-56`):
```cpp
uint32_t atOffset = sampChild->UnsignedAttribute("Offset");
sampleOffsets[bankId][atOffset] = sampChild->Attribute("Name");
```
Stores the raw XML Offset attribute (relative within bank) as the map key.

**Sample name lookup** (`ZAudio.cpp:174`):
```cpp
sample->fileName = sampleOffsets[bankIndex][sampleDataOffset];
```
where `sampleDataOffset = relPtr + audioSampleBankEntry.ptr` (absolute Audiotable offset).

**Key mismatch**: The stored key is a relative offset, but the lookup key is an absolute offset.
- Bank 1 (base=0): absolute == relative → lookup succeeds → named path
- Banks 2/3/5/6 (base≠0): absolute ≠ relative → lookup fails → empty string

**OTRExporter fallback** (`AudioExporter.cpp:67-73`):
```cpp
if (sampleOffsets[bankId].contains(sampleDataOffset) &&
    sampleOffsets[bankId][sampleDataOffset] != "") {
    path = "audio/samples/" + name + "_META";
} else {
    path = "audio/samples/sample_" + bankId + "_" + hex(sampleDataOffset) + "_META";
}
```

Result: 430 named (bank 1 only) + 19 fallback (banks 2/3/5/6) = 449 total.

### How Torch handles it (bug)

Name lookup (`OoTAudioFactory.cpp:311-312`):
```cpp
int sampleRelOff = (int32_t)readBE32(audioBank, sampleAddr + 4);
if (sampleNames.count(bankIndex) && sampleNames[bankIndex].count(sampleRelOff)) {
```
Uses **relative** offset as lookup key → matches YAML relative offset for ALL banks.

All 449 samples get named paths. Cross-bank duplicates (same name, different absolute offset)
overwrite each other in RegisterCompanionFile. Result: 441 named + 0 fallback.

## Fix
Change name lookup to use `sampleDataOffset` (absolute) instead of `sampleRelOff` (relative).
This matches ZAPDTR's accidental-but-correct behavior where only base-0 banks get named paths.

### Specific change (OoTAudioFactory.cpp lines 310-318):
```cpp
// Use absolute offset for name lookup (matches ZAPDTR behavior)
if (sampleNames.count(bankIndex) && sampleNames[bankIndex].count((int)sampleDataOffset)) {
    s.name = sampleNames[bankIndex][(int)sampleDataOffset];
} else {
    // Fallback: sample_BANKID_ABSOFFSET (matches OTRExporter format)
    std::ostringstream ss;
    ss << "sample_" << bankIndex << "_" << std::setfill('0') << std::setw(8)
       << std::hex << std::uppercase << sampleDataOffset;
    s.name = ss.str();
}
```

## Verification
- Reference: 430 named + 19 fallback = 449
- After fix: should match exactly
- The 19 fallback samples are in banks 2 (1), 3 (5), 5 (6), 6 (7)
