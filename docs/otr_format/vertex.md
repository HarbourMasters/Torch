@page otr_vertex Vertex Resource
# Vertex Resource (OVTX)

If the Resource Type is `OVTX` (`0x4F565458`), the header is immediately followed by the vertex array definition.

## Vertex Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 4 | u32 | Count |
| 0x44 | ... | [Vtx] | Array of Vtx structures |

### Vtx Structure

Each `Vtx` structure (16 bytes) represents a vertex for the N64 RCP:

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x00 | 2 | s16 | X Position |
| 0x02 | 2 | s16 | Y Position |
| 0x04 | 2 | s16 | Z Position |
| 0x06 | 2 | u16 | Flag |
| 0x08 | 2 | s16 | Texture Coordinate S (10.5 fixed point) |
| 0x0A | 2 | s16 | Texture Coordinate T (10.5 fixed point) |
| 0x0C | 1 | u8 | Color Red / Normal X |
| 0x0D | 1 | u8 | Color Green / Normal Y |
| 0x0E | 1 | u8 | Color Blue / Normal Z |
| 0x0F | 1 | u8 | Color Alpha |

## YAML Configuration

To define a vertex array asset in the configuration, use the `VTX` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `VTX`. |
| `offset` | Integer | The offset of the vertex array in the ROM/binary. |
| `count` | Integer | The number of Vtx structures to parse. |
| `symbol` | String | (Optional) The symbol name for the asset. |

Example:
```yaml
- type: VTX
  offset: 0x8000
  count: 32
  symbol: my_vertex_buffer
```
