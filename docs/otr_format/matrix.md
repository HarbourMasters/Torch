@page otr_matrix Matrix Resource
# Matrix Resource (OMTX)

If the Resource Type is `OMTX` (`0x4F4D5458`), the header is immediately followed by the matrix definition.

## Matrix Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 64 | Mtx | The 4x4 Matrix data. |

### Matrix Data

The matrix data consists of 16 values representing a 4x4 transformation matrix.
Depending on the configuration (`gbi.useFloats`), these values are stored either as:

*   **Floats (f32)**: 16 consecutive 32-bit floating point numbers.
*   **Fixed Point (s15.16)**: The standard N64 `Mtx` structure format, where integer parts and fractional parts are stored separately.

## YAML Configuration

To define a matrix asset in the configuration, use the `MTX` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `MTX`. |
| `offset` | Integer | The offset of the matrix in the ROM/binary. |
| `symbol` | String | (Optional) The symbol name for the asset. |

Example:
```yaml
- type: MTX
  offset: 0x5000
  symbol: my_matrix
```
