@page otr_vec3s Vector 3 Short Resource
# Vector 3 Short Resource (VC3S)

If the Resource Type is `VC3S` (`0x56433353`), the header is immediately followed by the vector array definition.

## Vec3s Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 4 | u32 | Count |
| 0x44 | ... | [Vec3s] | Array of Vec3s structures |

### Vec3s Structure

Each `Vec3s` consists of 3 consecutive 16-bit signed integers:

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x00 | 2 | s16 | X Coordinate |
| 0x02 | 2 | s16 | Y Coordinate |
| 0x04 | 2 | s16 | Z Coordinate |

## YAML Configuration

To define a Vec3s array asset in the configuration, use the `VEC3S` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `VEC3S`. |
| `offset` | Integer | The offset of the vector array in the ROM/binary. |
| `count` | Integer | The number of Vec3s structures to parse. |
| `symbol` | String | (Optional) The symbol name for the asset. |

Example:
```yaml
- type: VEC3S
  offset: 0x7000
  count: 20
  symbol: my_short_vector_array
```
