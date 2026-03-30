# Audio Sample Extraction (Step 4) — Corrected Approach

## Root Cause of Previous Crash
Used raw pointer arithmetic `BSWAP32(*(uint32_t*)(audioBank + offset))` instead
of Torch's standard `LUS::BinaryReader` pattern. Caused segfaults when offsets
were out of bounds or misaligned.

## Correct Pattern: BinaryReader
All reads from Audiobank/Audiotable must use `LUS::BinaryReader` with BE endianness:
```cpp
LUS::BinaryReader reader((char*)data, size);
reader.SetEndianness(Torch::Endianness::Big);
reader.Seek(offset, LUS::SeekOffsetType::Start);
uint32_t val = reader.ReadUInt32();
```

This is the same pattern used by OoTSceneFactory (ReadSubArray), OoTSkeletonFactory,
OoTCollisionFactory, and the existing NAudio v0 AudioManager.

## Data Loading
Audiobank and Audiotable are uncompressed ROM segments. Load as vectors:
```cpp
auto bankSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(1);
std::vector<uint8_t> audioBank(buffer.begin() + bankSeg.value(),
                                buffer.begin() + bankSeg.value() + bankSize);
```

DMA table provides sizes: Audiobank=0x2BDC0, Audiotable=0x451390 for PAL GC.
These should be computed from virt_end - virt_start.

## Pointer Chain (verified against ZAPDTR ZAudio.cpp lines 215-305)

### Font structure at fontPtr in Audiobank:
- `+0`: pointer to drum pointer array (relative to fontPtr)
- `+4`: pointer to SFX entry array (relative to fontPtr)
- `+8+i*4`: pointer to instrument i (relative to fontPtr)

### Font → Drums → Samples:
1. `drumListAddr = BE32(audioBank, fontPtr + 0) + fontPtr`
2. For each drum i: `drumEntryPtr = BE32(audioBank, drumListAddr + i*4)`
3. If non-null: `drumEntryPtr += fontPtr`
4. Drum entry at drumEntryPtr: byte0=releaseRate, byte1=pan, byte2=loaded
5. Sample entry pointer: `sampleAddr = BE32(audioBank, drumEntryPtr + 4) + fontPtr`
6. ParseSampleEntry at sampleAddr

### Font → Instruments → Sounds → Samples:
1. For instrument i: `instAddr = BE32(audioBank, fontPtr + 8 + i*4)`
2. If non-null: `instAddr += fontPtr`
3. Three sound entries at instAddr+8, instAddr+16, instAddr+24
4. Each: `sampleAddr = BE32(audioBank, soundAddr + 0) + fontPtr`
5. ParseSampleEntry at sampleAddr

### Font → SFX → Samples:
1. `sfxListAddr = BE32(audioBank, fontPtr + 4) + fontPtr`
2. For each sfx i at sfxListAddr + i*8:
3. `sampleAddr = BE32(audioBank, sfxListAddr + i*8) + fontPtr`
4. ParseSampleEntry at sampleAddr

### ParseSampleEntry at sampleAddr in Audiobank (16 bytes):
```
+0: origField (u32BE) — bits 28-31=codec, 24-25=medium, 22=unk26, 21=unk25, 0-23=size
+4: dataPtr (u32BE) — offset into Audiotable relative to sampleBankEntry.ptr
+8: loopPtr (u32BE) — offset into Audiobank relative to fontPtr
+12: bookPtr (u32BE) — offset into Audiobank relative to fontPtr
```

### Loop data at (loopPtr + fontPtr):
```
+0: start (i32BE)
+4: end (i32BE)
+8: count (i32BE)
+12: pad (i32BE)
+16: states[16] (i16BE each) — only if count != 0
```

### ADPCM book at (bookPtr + fontPtr):
```
+0: order (i32BE)
+4: npredictors (i32BE)
+8: books[order*npredictors*8] (i16BE each)
```

## Output Format (OSMP, v2)
```
[64-byte Torch header, type=0x4F534D50, version=2]
[codec:u8] [medium:u8] [unk_bit26:u8] [unk_bit25:u8]
[data_size:u32] [raw ADPCM data: data_size bytes]
[loop.start:u32] [loop.end:u32] [loop.count:u32]
[loop.states_count:u32] [loop.states: i16[] × count]
[book.order:u32] [book.npredictors:u32]
[book.books_count:u32] [book.books: i16[] × count]
```

## Sample Naming
From YAML sample bank entries: `sampleOffsets[bankId][relOffset] = name`
Companion file name: `samples/{name}_META`
Fallback: `samples/sample_{bankId}_{absOffset:08X}_META`

## Verification
- After implementation: 449 sample assets should pass
- Compare a few with `compare_asset.py`
- Total: 34,894 + 449 = 35,343 passing
