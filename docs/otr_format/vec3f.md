@page otr_vec3f Vector 3 Float Resource
# Vector 3 Float Resource (VC3F)

If the Resource Type is `VC3F` (`0x56433346`), the header is immediately followed by the vector array definition.

## Vec3f Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 4 | u32 | Count |
| 0x44 | ... | [Vec3f] | Array of Vec3f structures |

### Vec3f Structure

Each `Vec3f` consists of 3 consecutive 32-bit floating point numbers:

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x00 | 4 | f32 | X Coordinate |
| 0x04 | 4 | f32 | Y Coordinate |
| 0x08 | 4 | f32 | Z Coordinate |

## YAML Configuration

To define a Vec3f array asset in the configuration, use the `VEC3F` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `VEC3F`. |
| `offset` | Integer | The offset of the vector array in the ROM/binary. |
| `count` | Integer | The number of Vec3f structures to parse. |
| `symbol` | String | (Optional) The symbol name for the asset. |

Example:
```yaml
- type: VEC3F
  offset: 0x6000
  count: 10
  symbol: my_vector_array
```
