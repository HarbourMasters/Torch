# Plan: OoT Audio Factory (598 assets)

## Context
598 audio assets missing from O2R. Single YAML entry `audio/audio` type `OOT:AUDIO`. No factory exists. Audio data spans multiple ROM segments that must be parsed into 598 individual files.

## Asset Breakdown
| Category | Count | Resource Type Code | Description |
|----------|-------|--------------------|-------------|
| audio/audio | 1 | OAUD (0x4F415544) | Empty 64-byte header (v2) |
| audio/samples | 449 | OSMP (0x4F534D50) | ADPCM samples with loop/book metadata |
| audio/fonts | 38 | OSFT (0x4F534654) | SoundFont definitions (instruments, drums, SFX) |
| audio/sequences | 110 | OSEQ (0x4F534551) | Music/SFX sequence data |

## ROM Structure (Critical: Multiple Segments)

Audio data is NOT in a single segment. Four ROM regions are involved:

| DMA Entry | PAL GC Offset | Contents |
|-----------|---------------|----------|
| code | 0x00A580D0 (Yaz0) | Audio table headers (offsets, counts) |
| Audiobank | 0x0000D0D0 | Sample metadata + soundfont data |
| Audiotable | 0x00088910 | Raw ADPCM sample PCM data |
| Audioseq | 0x00038E90 | Sequence/music data |

The audio table structures are in the **code** segment at XML-specified offsets:
- `SoundFontTableOffset` → points to font table in code
- `SequenceTableOffset` → points to sequence table in code
- `SampleBankTableOffset` → points to sample bank table in code
- `SequenceFontTableOffset` → points to sequence-to-font mapping in code

Each table has a header: `numEntries(i16 BE)`, padding, then 16-byte entries with `ptr(u32)`, `size(u32)`, `medium(u8)`, `cachePolicy(u8)`, `data1-3(i16)`.

## Binary Formats (verified against OTRExporter)

### Sample (OSMP, version 2)
```
[64-byte Torch header]
[codec: u8] [medium: u8] [unk_bit26: u8] [unk_bit25: u8]
[data_size: u32] [raw ADPCM data: data_size bytes]
[loop.start: u32] [loop.end: u32] [loop.count: u32]
[loop.states_count: u32] [loop.states: i16[] × count]
[book.order: u32] [book.npredictors: u32]
[book.books_count: u32] [book.books: i16[] × count]
```
Note: All multi-byte fields written via BinaryWriter (endianness TBD — need to verify if audio uses BE or LE).

### SoundFont (OSFT, version 2)
```
[64-byte Torch header]
[font_index: u32] [medium: u8] [cachePolicy: u8]
[data1: u16] [data2: u16] [data3: u16]
[num_drums: u32] [num_instruments: u32] [num_sfx: u32]
FOR EACH DRUM:
  [releaseRate: u8] [pan: u8] [loaded: u8]
  [num_envelopes: u32] [envelope entries: (i16 delay, i16 arg) × count]
  [has_sample: u8] [sample_ref: string] [tuning: float]
FOR EACH INSTRUMENT:
  [isValid: u8] [loaded: u8] [normalRangeLo: u8] [normalRangeHi: u8]
  [releaseRate: u8] [num_envelopes: u32] [envelopes...]
  [lowNotesSound entry] [normalNotesSound entry] [highNotesSound entry]
FOR EACH SFX:
  [sound font entry]
```

### Sequence (OSEQ, version 2)
```
[64-byte Torch header]
[sequence_size: u32] [sequence_data: bytes]
[sequence_index: u8] [medium: u8] [cachePolicy: u8]
[num_font_indices: u32] [font_ids: u8[] × count]
```

## Implementation Approach

### Phase 1: Parse audio table headers
- Read the code segment
- Extract table offsets from YAML (or hardcode from known XML values)
- Parse each table's entry count and per-entry metadata

### Phase 2: Extract samples
- For each sample bank entry, read Audiobank metadata
- For each sample within a bank, read Audiotable raw data
- Write OSMP companion files with loop/book metadata

### Phase 3: Extract fonts
- For each font entry, parse instrument/drum/SFX tables from Audiobank
- Resolve sample references (sample offset → sample name)
- Write OSFT companion files

### Phase 4: Extract sequences
- For each sequence entry, read raw data from Audioseq
- Read font index mapping from SequenceFontTable
- Write OSEQ companion files

### Challenge: Multiple ROM Segments
The YAML declares a single segment (128 at 0x00A580D0 = code). But audio data spans 4 DMA entries. The factory will need to load Audiobank, Audiotable, and Audioseq data separately using their DMA table offsets.

Options:
1. Add extra segments to the audio YAML (Audiobank, Audiotable, Audioseq)
2. Load the DMA entries directly in the factory using ROM data
3. Use Companion's segment system to map all 4 segments

### Challenge: Name Resolution
Sample and font names come from Shipwright's XML audio declarations. Without these, we'd need to use offset-based fallback names (`sample_B_OFFSET_META`). The `zapd_to_torch.py` converter would need to extract name mappings from the XML.

## Estimated Scope
~600-900 lines of new C++ code. Complex but mechanical — parsing well-documented structures and writing known binary formats.

## Files to Create/Modify
- `src/factories/oot/OoTAudioFactory.cpp` — new factory (main implementation)
- `src/factories/oot/OoTAudioFactory.h` — new header
- `src/Companion.cpp` — register OOT:AUDIO factory
- `soh/tools/zapd_to_torch.py` — extract audio name mappings from XML
- `soh/assets/yml/pal_gc/audio/audio.yml` — add audio table offsets and segment info

## Verification
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --failures-only`
- Should show 598 fewer not-generated, 0 failures
- Compare individual samples/fonts/sequences with `compare_asset.py`
