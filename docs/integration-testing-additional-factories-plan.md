# Integration Testing: Additional SM64 Factory Tests

## Context

The initial integration test infrastructure (6 tests) covers TEXTURE, VTX, BLOB, and SM64:COLLISION. This plan adds integration tests for the remaining SM64-specific and common factories used by SM64, exercising the full Companion pipeline for each.

## Factories to Test

### Already tested
- TEXTURE (RGBA16)
- VTX
- BLOB
- SM64:COLLISION

### Common factories (used by SM64)
| Factory | Resource Type | Reference YAML | Segment |
|---------|--------------|----------------|---------|
| GFX | DisplayList (0x4F444C54) | amp electricity sub dl, offset 0x2B68 | seg 8, 0x1F2200 |
| LIGHTS | Lights (0x46669697) | cannon_lid lights, offset 0x4040 | seg 8, 0x1F2200 |

### SM64-specific factories
| Factory | Resource Type | Reference Source | Segment |
|---------|--------------|-----------------|---------|
| SM64:ANIM | Anim (0x414E494D) | anims.yml, offset 0x4EC690 | global (no segment) |
| SM64:DIALOG | SDialog (0x53444C47) | dialogs.yml, offset 0xFFC8 | seg 2, 0x108A40 |
| SM64:TEXT | Blob (0x4F424C42) | acts.yml, offset 0x10FD4 | seg 2, 0x108A40 |
| SM64:DICTIONARY | Dictionary (0x44494354) | strings.yml (keys map) | special - no offset |
| SM64:GEO_LAYOUT | Blob (0x4F424C42) | mario geo, offset 0x170002E0 | seg 0x17, 0x1279B0 |
| SM64:MACRO | MacroObject (0x4D41434F) | castle_inside macros | seg 7, compressed |
| SM64:MOVTEX | Movtex (0x4D4F5654) | castle_inside movtex | seg 7, compressed |
| SM64:MOVTEX_QUAD | MovtexQuad (0x4D4F5651) | castle_inside movtex quad | seg 7, compressed |
| SM64:PAINTING | Painting (0x504E5420) | castle_inside bob_painting | seg 7, compressed |
| SM64:PAINTING_MAP | PaintingData (0x504E5444) | castle_inside painting map | seg 7, compressed |
| SM64:TRAJECTORY | Trajectory (0x5452414A) | castle_inside mips trajectory | seg 7, compressed |
| SM64:BEHAVIOR_SCRIPT | BehaviorScript (0x42485653) | not in Ghostship YAMLs yet | TBD |
| SM64:LEVEL_SCRIPT | LevelScript (0x4C564C53) | not in Ghostship YAMLs yet | TBD |
| SM64:WATER_DROPLET | WaterDroplet (0x57545244) | not in Ghostship YAMLs yet | TBD |

## Binary Export Formats

### Simple count+data pattern
- **LIGHTS**: header + raw Lights1Raw struct (fixed 48 bytes total: ambient 24 + directional 24)
- **SM64:TEXT**: header + uint32 size + char data (exports as Blob)
- **SM64:GEO_LAYOUT**: header + uint32 size + data (exports as Blob)
- **SM64:MACRO**: header + uint32 size + int16 data
- **SM64:MOVTEX**: header + uint32 size + int16 data
- **SM64:TRAJECTORY**: header + uint32 size + trajectory entries (4x int16 each)

### Complex formats
- **GFX (DisplayList)**: header + int8 GBI version + padding to 8-byte align + marker (G_MARKER<<24, 0xBEEFBEEF) + hash pair + variable command pairs with OTR encoding
- **SM64:ANIM**: header + 6x int16 fields + uint64 length + uint32 indices count + indices + uint32 entries count + entries
- **SM64:DIALOG**: header + uint32 unused + int8 linesPerBox + int16 leftOffset + int16 width + uint32 text size + text data
- **SM64:DICTIONARY**: header + uint32 dict size + (string key + uint32 value size + value data) per entry
- **SM64:MOVTEX_QUAD**: header + uint32 count + (int16 id + uint64 hash) per quad
- **SM64:PAINTING**: header + large fixed struct with floats and uint64 hashes
- **SM64:PAINTING_MAP**: header + uint32 total + int16 mappingsSize + mappings + int16 groupsSize + groups

## Compressed Segment Handling

Several factories (MACRO, MOVTEX, MOVTEX_QUAD, PAINTING, PAINTING_MAP, TRAJECTORY) reference assets in compressed level data (e.g., castle_inside at 0x396340). The YAML `:config:` needs a `compression:` section:

```yaml
:config:
  compression:
    offset: 0x396340
  segments:
    - [ 0x7, 0x396340 ]
  force: true
```

Torch's Decompressor handles MIO0 decompression automatically when this is present.

## Implementation Order

### Commit 1: GFX + LIGHTS (uncompressed, segment 8)
- `test_gfx_basic.yml` — amp electricity sub DL at 0x2B68
- `test_lights_basic.yml` — cannon_lid lights at 0x4040
- Validation: header magic, size > 0x40

### Commit 2: SM64:ANIM + SM64:DIALOG + SM64:TEXT
- `test_anim.yml` — anim_00 at 0x4EC690 (global offset, no segment needed)
- `test_dialog.yml` — dialog_00 at 0xFFC8 (seg 2, 0x108A40)
- `test_text.yml` — act_00 at 0x10FD4 (seg 2, 0x108A40)
- Validation: header magic, field-level checks where format is simple

### Commit 3: SM64:GEO_LAYOUT
- `test_geo_layout.yml` — mario geo at 0x170002E0 (seg 0x17, 0x1279B0)
- Note: needs external_files or standalone geo entry
- Validation: exports as Blob, header + size + data

### Commit 4: Compressed level factories (castle_inside)
- `test_macro.yml` — castle_inside macros
- `test_movtex.yml` — castle_inside movtex
- `test_movtex_quad.yml` — castle_inside movtex quad
- `test_trajectory.yml` — castle_inside trajectory
- All use compression offset 0x396340, segment 7
- Validation: header magic, size > 0

### Commit 5: Painting factories (castle_inside)
- `test_painting.yml` — bob_painting
- `test_painting_map.yml` — painting texture map
- Same compressed segment as commit 4
- Validation: header magic, size checks

### Commit 6: SM64:DICTIONARY
- `test_dictionary.yml` — global dictionary with keys map
- Special case: no offset, uses keys map
- Validation: header magic, dict size > 0

### Future (when reference YAMLs become available)
- SM64:BEHAVIOR_SCRIPT
- SM64:LEVEL_SCRIPT
- SM64:WATER_DROPLET

## Validation Strategy

For all types, validate:
1. Header: magic bytes, version=0, DEADBEEF marker
2. Size: total size >= 0x40 (header) + minimum payload
3. Type-specific: count fields > 0 where applicable, size consistency

For complex types (GFX, PAINTING), validation is limited to header + non-empty payload since the internal structure depends on ROM data and cross-references.

## Critical Files

| File | Role |
|------|------|
| `tests/integration/SM64IntegrationTest.cpp` | Add new TEST_F cases |
| `tests/integration/sm64/us/assets/*.yml` | New YAML per test case |
| Ghostship reference YAMLs | Read-only — offset/segment reference |
