# Audio Font Extraction (Step 5)

## Context
38 audio fonts are the last remaining audio assets (35,343/35,386 passing, 38 fonts + 5 non-audio = 43 not generated). Fonts describe how instruments, drums, and SFX reference samples with tuning/envelope data. This is Step 5 of the audio implementation plan.

## Two changes needed

### 1. Add font names to YAML (zapd_to_torch.py)

The XML has `<Soundfont Name="00_Sound_Effects_1" Index="0"/>` entries that `convert_audio()` doesn't extract. Add font name extraction:

```python
# In convert_audio(), after sample extraction:
fonts = []
for child in elem:
    if child.tag == "Soundfont":
        fonts.append({"name": child.get("Name"), "index": int(child.get("Index", 0))})
if fonts:
    entry["fonts"] = sorted(fonts, key=lambda f: f["index"])
```

**File**: `soh/tools/zapd_to_torch.py` → `convert_audio()` (line ~200)

Then regenerate YAML.

### 2. Implement font companion file export (OoTAudioFactory.cpp)

Add Step 5 after the existing sample extraction (after line 416).

#### Binary format (OSFT, version 2)

Verified against `AudioExporter.cpp:258-311` and reference O2R hex dump:

```
[64-byte Torch header, type=OoTAudioSoundFont (0x4F534654), version=2]

Font metadata:
  u32     font_index
  u8      medium
  u8      cachePolicy
  i16     data1          (LE, raw value from ParseAudioTable)
  i16     data2          (LE, raw value from ParseAudioTable)
  i16     data3          (LE, raw value from ParseAudioTable)

Counts:
  u32     numDrums
  u32     numInstruments
  u32     numSfx

For each drum:
  u8      releaseRate
  u8      pan
  u8      loaded
  EnvData:
    u32   envCount
    For each: i16 delay, i16 arg
  u8      sampleExists (0 or 1)
  string  sampleRef     (length-prefixed: i32 len + chars)
  float   tuning

For each instrument:
  u8      isValidInstrument (0 or 1)
  u8      loaded
  u8      normalRangeLo
  u8      normalRangeHi
  u8      releaseRate
  EnvData (same format as drum)
  SoundFontEntry (lowNotes):
    u8    exists (0 or 1)
    if exists: u8 padding(1), string sampleRef, float tuning
  SoundFontEntry (normalNotes): same
  SoundFontEntry (highNotes): same

For each SFX:
  SoundFontEntry: same format as above
```

#### Key parsing details (from ZAudio.cpp:215-305)

**Font structure at fontPtr in Audiobank:**
- `+0`: ptr to drum pointer array (relative to fontPtr)
- `+4`: ptr to SFX entry array (relative to fontPtr)
- `+8 + i*4`: ptr to instrument i (relative to fontPtr)

**Drum entry** (at drumEntryPtr):
- `+0`: releaseRate (u8), `+1`: pan (u8), `+2`: loaded (u8)
- `+4`: sample entry ptr (relative to fontPtr) — feed to ParseSampleEntry
- `+8`: tuning (float BE)
- `+12`: envelope ptr (relative to fontPtr)

**Instrument entry** (at instPtr):
- `+0`: loaded (u8), `+1`: normalRangeLo, `+2`: normalRangeHi, `+3`: releaseRate
- `+4`: envelope ptr (relative to fontPtr)
- `+8/+16/+24`: three SoundFontEntry refs (low/normal/high notes)

**SoundFontEntry** (8 bytes at offset):
- `+0`: sample ptr (4 bytes, 0 if null) → relative to fontPtr
- `+4`: tuning (float BE)

**Instrument high notes condition** (ZAudio.cpp:296-297):
```cpp
if (ptr != 0 && instrument.normalRangeHi != 0x7F)
```
Only parse highNotesSound if pointer is non-zero AND normalRangeHi != 127.

**Envelope parsing** (ZAudio.cpp:74-93):
- Read pairs of (i16 delay, i16 arg) from audiobank
- Continue until delay < 0 (terminator entry is included in output)

#### Sample reference resolution

Reuse the existing `sampleMap` and `sampleNames` from Step 4. For each sample pointer encountered in a font:
1. Compute absolute offset: `dataRelPtr + sampleBankTable[sampleBankId].ptr`
2. Look up in `sampleMap` to find the SampleInfo with its resolved name
3. Build reference path: `"audio/samples/" + name + "_META"`
4. If sample is null (ptr==0), write empty string `""`

This matches `GetSampleEntryReference()` in AudioExporter.cpp:63-77.

#### Font names

Read from YAML `fonts` list (added in step 1). Fallback: `std::to_string(i) + "_Font"`.

**Companion file path**: `"fonts/" + fontName`

## Implementation order

1. Update `zapd_to_torch.py` → add font name extraction
2. Regenerate YAML: `python3 soh/tools/zapd_to_torch.py ...`
3. Add font extraction to `OoTAudioFactory.cpp` after sample extraction
4. Build and test

## Files to modify
- `soh/tools/zapd_to_torch.py` — add Soundfont extraction in `convert_audio()`
- `src/factories/oot/OoTAudioFactory.cpp` — add Step 5 font extraction after line 416

## Verification
- Build: `cmake --build build -j32`
- Test: `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --category audio`
- Expected: 598/598 audio assets pass (was 560/598)
- Full test: 35,381/35,386 (only 5 non-audio remaining)
