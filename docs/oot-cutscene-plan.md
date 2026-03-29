# Plan: Fix OoT Cutscene Binary Export

## Context
106 scene failures are cutscene-related. The current implementation has correct size calculation (command-aware parser) but writes raw BE ROM bytes. OTRExporter re-serializes cutscene data from BE ROM format into LE with field re-packing using CMD_HH/CMD_BBH/CMD_HBB macros. Raw copying doesn't match.

## Root Cause
ROM stores fields as sequential BE values. OTRExporter re-packs them into uint32 words using macros:
- `CMD_HH(a, b)` = `(b << 16) | a` — packs two int16 into one uint32
- `CMD_BBH(a, b, c)` = `a | (b << 8) | (c << 16)` — packs byte, byte, halfword
- `CMD_HBB(a, b, c)` = `a | (b << 16) | (c << 24)` — packs halfword, byte, byte

Then BinaryWriter writes these uint32 values in LE. This is NOT a simple BE→LE byte swap.

## Approach: Parse BE fields, re-serialize with macro packing

Replace the raw copy loop in `SetCutscenes` handler with a proper parser/serializer.

### Command Types and Their Formats

**Standard header (all commands except camera/transition/destination):**
```
Write: CMD_W(commandID), CMD_W(entryCount)
```

**Camera Splines (0x01, 0x02, 0x05, 0x06):**
- Header: `CMD_W(cmdId)`, `CMD_HH(0x0001, startFrame)`, `CMD_HH(endFrame, 0x0000)`
- Per point (0x10 bytes ROM → 4 words):
  - `CMD_BBH(continueFlag, cameraRoll, nextPointFrame)`
  - `float viewAngle`
  - `CMD_HH(posX, posY)`
  - `CMD_HH(posZ, unused)`
- Terminated by point with continueFlag == 0xFF

**0x30-byte entry commands (0x03 misc, 0x04 lighting, 0x56/0x57/0x7C BGM, actor cues 0x0A-0x27+):**
- Per entry (0x30 bytes ROM → 12 words):
  - `CMD_HH(base, startFrame)`, `CMD_HH(endFrame, rotXorPad)`
  - For actor cues: `CMD_HH(rotY, rotZ)`, then 9 × `CMD_W(int32/float)`
  - For misc/lighting/BGM: `CMD_HH(endFrame, pad)`, then 7 × `CMD_W(unused)`, then 3 × `0x00000000`

**0x0C-byte entry commands (0x09 rumble, 0x13 textbox, 0x8C settime):**
- 0x09 rumble: `CMD_HH(base, startFrame)`, `CMD_HBB(endFrame, sourceStrength, duration)`, `CMD_BBH(decreaseRate, unk09, unk0A)`
- 0x13 textbox: `CMD_HH(base, startFrame)`, `CMD_HH(endFrame, type)`, `CMD_HH(textId1, textId2)`
- 0x8C settime: `CMD_HH(base, startFrame)`, `CMD_HBB(endFrame, hour, minute)`, `0x00000000`

**Special commands:**
- 0x2D transition: `CMD_W(0x2D)`, `CMD_W(1)`, `CMD_HH(type, startFrame)`, `CMD_HH(endFrame, endFrame)`
- 0x3E8 destination: `CMD_W(0x3E8)`, `CMD_W(1)`, `CMD_HH(destId, startFrame)`, `CMD_HH(endFrame, endFrame)`

### Implementation Structure

In `OoTSceneFactory.cpp`, add helper functions:

```cpp
static uint32_t CMD_HH(uint16_t a, uint16_t b) { return ((uint32_t)b << 16) | (uint32_t)a; }
static uint32_t CMD_BBH(uint8_t a, uint8_t b, uint16_t c) { return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16); }
static uint32_t CMD_HBB(uint16_t a, uint8_t b, uint8_t c) { return (uint32_t)a | ((uint32_t)b << 16) | ((uint32_t)c << 24); }
```

Then the SetCutscenes handler:
1. Use existing command-aware size calculator to get total size
2. Create a fresh BE reader over the cutscene data
3. Read CS_BEGIN (numCommands, endFrame) → write as `CMD_W(numCommands), CMD_W(endFrame)`
4. For each command: read cmdID, dispatch to per-type handler that reads BE fields and writes packed LE words
5. Write CS_END (0xFFFFFFFF, 0x00000000)
6. Prepend the word count

### Files to Modify
- `src/factories/oot/OoTSceneFactory.cpp` — replace cutscene raw copy with parse/re-serialize

### Verification
1. `python3 soh/tools/compare_asset.py scenes/nonmq/bdan_scene/bdan_sceneSet_013700CutsceneData_013080` — should pass
2. Full test should show ~106 fewer failures (all cutscene-related)
3. No regressions
