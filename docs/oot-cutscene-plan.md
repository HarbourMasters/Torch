# Plan: Fix OoT Cutscene Binary Export

## Context
106 scene failures are cutscene-related. The current implementation copies raw uint32 words until `0xFFFFFFFF`, but `0xFFFFFFFF` legitimately appears in cutscene data (camera markers, padding). This causes early termination, producing truncated cutscene files.

The ROM cutscene format and OTRExporter output are byte-identical — no transformation needed. We just need to find the correct end of the cutscene data.

## Approach: Command-aware size calculation

Replace the naive `0xFFFFFFFF` scan with a command-aware parser that reads the cutscene structure to determine total size, then copies the raw data.

### Cutscene ROM Structure
```
CS_BEGIN: [numCommands: u32] [endFrame: u32]
For each command:
  [commandID: u32] [entryCount: u32] [entries...]
CS_END: [0xFFFFFFFF] [0x00000000]
```

Each command has a fixed entry size based on its ID:

| Command ID | Entry Size (bytes) | Notes |
|---|---|---|
| 0x01, 0x02, 0x05, 0x06 | 0x10 | Camera splines — BUT entry count in header is start/end frame, actual entries are terminated by continueFlag == -1. Need special handling. |
| 0x03, 0x04, 0x56, 0x57, 0x7C | 0x30 | Misc, Lighting, BGM commands |
| 0x09, 0x13, 0x8C | 0x0C | Rumble, Textbox, SetTime |
| 0x2D | special | Scene transition: always 0x08 bytes after header (1 entry × 0x08) |
| 0x3E8 | special | Destination: always 0x08 bytes after header |
| 0x0A-0x27 (most) | 0x30 | Actor/Player cues |
| default | 0x30 | Safe default for unknown command types |

### Camera Command Special Case
Camera commands (0x01, 0x02, 0x05, 0x06) are different:
- Header word 1 is NOT an entry count — it's `CMD_HH(0x0001, startFrame)`
- Header word 2 is `CMD_HH(endFrame, 0x0000)`
- Entries are 0x10 bytes each, terminated when `continueFlag` (first byte) is 0xFF (-1)
- So: read 0x10-byte entries until first byte of an entry is 0xFF, then that entry is the last

### Implementation

**File:** `src/factories/oot/OoTSceneFactory.cpp` (lines 764-806)

Replace the cutscene copy loop with:

```cpp
// Calculate cutscene data size by parsing command structure
auto csSizeReader = ReadSubArray(buffer, cmdArg2, 0x10000);
uint32_t numCommands = csSizeReader.ReadUInt32();  // CS_BEGIN word 0
csSizeReader.ReadUInt32();  // endFrame (skip)

for (uint32_t cmd = 0; cmd < numCommands; cmd++) {
    uint32_t cmdId = csSizeReader.ReadUInt32();
    if (cmdId == 0xFFFFFFFF) break;  // CS_END (shouldn't happen before numCommands)

    uint32_t word2 = csSizeReader.ReadUInt32();

    if (cmdId == 1 || cmdId == 2 || cmdId == 5 || cmdId == 6) {
        // Camera splines: read additional header word, then 0x10-byte entries until continueFlag == -1
        csSizeReader.ReadUInt32();  // CMD_HH(endFrame, 0)
        while (true) {
            uint8_t continueFlag = csSizeReader.ReadUByte();
            csSizeReader.Seek(csSizeReader.GetBaseAddress() + 0x0F);  // skip rest of 0x10-byte entry
            if (continueFlag == 0xFF) break;
        }
    } else if (cmdId == 0x2D || cmdId == 0x3E8) {
        // Scene transition / Destination: 0x08 bytes of data after header
        csSizeReader.Seek(csSizeReader.GetBaseAddress() + 0x08);
    } else {
        // Standard command: entryCount entries of known size
        uint32_t entryCount = word2;
        uint32_t entrySize = getEntrySize(cmdId);
        csSizeReader.Seek(csSizeReader.GetBaseAddress() + entryCount * entrySize);
    }
}
// Skip CS_END (0xFFFFFFFF + 0x00000000)
csSizeReader.ReadUInt32();
csSizeReader.ReadUInt32();

uint32_t totalCsSize = csSizeReader.GetBaseAddress();
```

Then copy `totalCsSize` bytes raw from ROM (same approach as now, but with correct size).

Helper function:
```cpp
static uint32_t getCutsceneEntrySize(uint32_t cmdId) {
    switch (cmdId) {
        case 0x09: case 0x13: case 0x8C: return 0x0C;
        default: return 0x30;  // Actor cues, misc, lighting, BGM
    }
}
```

### Why raw copy works
The ROM format and OTRExporter output are byte-identical. OTRExporter re-serializes from parsed objects but produces the same bytes. We can skip the parse/re-serialize step and just copy the correct number of bytes.

### Files to Modify
1. `src/factories/oot/OoTSceneFactory.cpp` — replace cutscene copy loop (~lines 764-806) with command-aware size calculator + raw copy

### Verification
1. `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64 --failures-only` — 106 fewer failures expected
2. `python3 soh/tools/compare_asset.py scenes/nonmq/bdan_scene/bdan_sceneSet_013700CutsceneData_013080` — should pass
3. No regressions in other categories
