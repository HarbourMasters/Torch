@page otr_generic_array Generic Array Resource
# Generic Array Resource (GARR)

If the Resource Type is `GARR` (`0x47415252`), the header is immediately followed by the generic array definition. This resource type is used to store arrays of primitive types or vectors.

## Generic Array Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 4 | u32 | Array Type (Enum) |
| 0x44 | 4 | u32 | Count |
| 0x48 | ... | ... | Array Data |

### Array Types

The `Array Type` field determines the type of data stored in the array.

| Value | Name | Size (bytes) | Description |
| :--- | :--- | :--- | :--- |
| 0 | u8 | 1 | Unsigned 8-bit integer |
| 1 | s8 | 1 | Signed 8-bit integer |
| 2 | u16 | 2 | Unsigned 16-bit integer |
| 3 | s16 | 2 | Signed 16-bit integer |
| 4 | u32 | 4 | Unsigned 32-bit integer |
| 5 | s32 | 4 | Signed 32-bit integer |
| 6 | u64 | 8 | Unsigned 64-bit integer |
| 7 | f32 | 4 | 32-bit Float |
| 8 | f64 | 8 | 64-bit Double |
| 9 | Vec2f | 8 | Vector of 2 floats |
| 10 | Vec3f | 12 | Vector of 3 floats |
| 11 | Vec3s | 6 | Vector of 3 shorts |
| 12 | Vec3i | 12 | Vector of 3 integers |
| 13 | Vec3iu | 12 | Vector of 3 unsigned integers |
| 14 | Vec4f | 16 | Vector of 4 floats |
| 15 | Vec4s | 8 | Vector of 4 shorts |

## YAML Configuration

To define a generic array in the configuration, use the `ARRAY` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `ARRAY`. |
| `offset` | Integer | The offset of the array in the ROM/binary. |
| `count` | Integer | The number of elements in the array. |
| `array_type` | String | The type of the elements (see table below). |
| `symbol` | String | (Optional) The symbol name for the array. |

### Supported Array Types (YAML)

- `u8`
- `s8`
- `u16`
- `s16`
- `u32`
- `s32`
- `u64`
- `f32`
- `f64`
- `Vec2f`
- `Vec3f`
- `Vec3s`
- `Vec3i`
- `Vec3iu`
- `Vec4f`
- `Vec4s`

Example:
```yaml
- type: ARRAY
  offset: 0xB000
  count: 10
  array_type: u32
  symbol: my_u32_array
```
