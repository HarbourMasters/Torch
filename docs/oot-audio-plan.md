# Plan: OoT Audio Factory (598 assets)

## Context
598 audio assets missing from O2R. Single YAML entry `audio/audio` type `OOT:AUDIO`. Audio data spans 4 ROM segments (code, Audiobank, Audiotable, Audioseq).

## Asset Breakdown
| Category | Count | Resource Type | Description |
|----------|-------|---------------|-------------|
| audio/audio | 1 | OAUD (0x4F415544) | Empty 64-byte header (v2) |
| audio/samples | 449 | OSMP (0x4F534D50) | ADPCM samples with loop/book metadata |
| audio/fonts | 38 | OSFT (0x4F534654) | SoundFont definitions |
| audio/sequences | 110 | OSEQ (0x4F534551) | Music/SFX sequence data |

## Incremental Implementation Steps

### Step 0: YAML setup
Add the audio table offsets and additional segment references to `audio.yml`. The factory needs to know where the audio tables are in the code segment, and needs access to Audiobank, Audiotable, and Audioseq ROM data.

**Verify**: YAML parses without errors, factory gets called (even if it returns nullopt).

**Key question**: How to get the table offsets? Options:
- A: Hardcode in factory for PAL GC (quick, not portable)
- B: Add to audio.yml from zapd_to_torch.py (need to find offsets in Shipwright XML)
- C: Read from ROM (the code segment has known structures)

Check Shipwright's XML for the audio table offset attributes.

### Step 1: Main audio entry + skeleton factory
Register `OOT:AUDIO` factory. Parse returns successfully. Export writes just the 64-byte OAUD header as the main `audio/audio` entry. No companion files yet.

**Verify**: `audio/audio` passes in test_assets (+1 asset). Compare with `compare_asset.py`.

### Step 2: Load all 4 ROM segments
Read Audiobank, Audiotable, Audioseq from ROM using DMA table offsets. Parse the three audio table headers from the code segment (count + per-entry offset/size/medium/cachePolicy).

**Verify**: Log the table counts — should be 38 fonts, ~110 sequences, ~5 sample banks. No output change yet, just parsing validation.

### Step 3: Extract sequences (simplest format)
Sequences are the simplest — just raw byte data + metadata. For each sequence entry:
- Read raw sequence data from Audioseq at the entry's offset
- Read font index mapping from SequenceFontTable
- Write OSEQ companion file

**Verify**: 110 sequence assets pass (+110). Compare a few with `compare_asset.py`.

### Step 4: Extract samples
For each sample bank entry, parse the Audiobank to find individual samples. For each sample:
- Read codec, medium, flags from Audiobank
- Read raw ADPCM data from Audiotable
- Read loop metadata and ADPCM book from Audiobank
- Write OSMP companion file

**Verify**: 449 sample assets pass (+449). This is the largest batch.

### Step 5: Extract fonts
For each font entry, parse instruments/drums/SFX from Audiobank. Each references samples by offset → resolve to sample path strings. Write OSFT companion files.

**Verify**: 38 font assets pass (+38). All 598 audio assets complete.

## ROM Structure

### DMA entries (PAL GC)
| Entry | ROM Offset | Compressed | Contents |
|-------|-----------|------------|----------|
| code | 0x00A580D0 | Yaz0 | Audio table headers at known offsets |
| Audiobank | 0x0000D0D0 | No | Sample metadata + soundfont structures |
| Audiotable | 0x00088910 | No | Raw ADPCM sample PCM data |
| Audioseq | 0x00038E90 | No | Sequence/music binary data |

### Audio table structure (in code segment)
```
Offset 0x00: numEntries (i16 BE)
Offset 0x02: padding
Offset 0x04: romAddr (i16 BE)
Offset 0x10+: Entry array (16 bytes each):
  +0: ptr (u32 BE) — offset into Audiobank/Audiotable/Audioseq
  +4: size (u32 BE)
  +8: medium (u8)
  +9: cachePolicy (u8)
  +10: data1 (i16 BE)
  +12: data2 (i16 BE)
  +14: data3 (i16 BE)
```

### Audio table offsets in code segment
Need to determine from Shipwright XML. Check:
- `Shipwright/soh/assets/xml/GC_NMQ_PAL_F/audio/` for XML attributes
- `SoundFontTableOffset`, `SequenceTableOffset`, `SampleBankTableOffset`, `SequenceFontTableOffset`

## Binary Formats

### Sample (OSMP, v2)
```
[64-byte header] [codec:u8] [medium:u8] [unk26:u8] [unk25:u8]
[data_size:u32] [raw data:bytes]
[loop.start:u32] [loop.end:u32] [loop.count:u32]
[loop.states_count:u32] [loop.states:i16[]]
[book.order:u32] [book.npredictors:u32] [book.count:u32] [book.books:i16[]]
```

### Sequence (OSEQ, v2)
```
[64-byte header] [size:u32] [data:bytes]
[index:u8] [medium:u8] [cachePolicy:u8]
[num_fonts:u32] [font_ids:u8[]]
```

### SoundFont (OSFT, v2)
```
[64-byte header] [font_index:u32] [medium:u8] [cachePolicy:u8]
[data1:u16] [data2:u16] [data3:u16]
[num_drums:u32] [num_instruments:u32] [num_sfx:u32]
[drum entries...] [instrument entries...] [sfx entries...]
```

## Name Resolution
Sample/font/sequence names come from Shipwright XML. zapd_to_torch.py would need to extract these and add to the YAML. Fallback: offset-based names (`sample_0_00001234_META`).

## Files to Create/Modify
- `src/factories/oot/OoTAudioFactory.cpp` — new
- `src/factories/oot/OoTAudioFactory.h` — new
- `src/Companion.cpp` — register OOT:AUDIO
- `soh/tools/zapd_to_torch.py` — extract audio metadata from XML
- `soh/assets/yml/pal_gc/audio/audio.yml` — add table offsets + segment refs

## Verification per step
Each step adds assets that can be independently verified:
1. Step 1: +1 (audio/audio)
2. Step 3: +110 (sequences)
3. Step 4: +449 (samples)
4. Step 5: +38 (fonts)
Total: +598
