@page otr_float Float Resource
# Float Resource (OFLT)

If the Resource Type is `OFLT` (`0x4F464C54`), the header is immediately followed by the float array definition.

## Float Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 4 | u32 | Count |
| 0x44 | ... | [f32] | Float Data |

## YAML Configuration

To define a float array asset in the configuration, use the `F32` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `F32`. |
| `offset` | Integer | The offset of the float array in the ROM/binary. |
| `count` | Integer | The number of float values to parse. |
| `symbol` | String | (Optional) The symbol name for the asset. |

Example:
```yaml
- type: F32
  offset: 0x3000
  count: 16
  symbol: my_float_array
```
