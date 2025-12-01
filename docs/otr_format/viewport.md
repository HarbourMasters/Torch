@page otr_viewport Viewport Resource
# Viewport Resource (OVPT)

If the Resource Type is `OVPT` (`0x4F565054`), the header is immediately followed by the viewport definition.

## Viewport Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 16 | Vp | The Vp structure containing scale and translation parameters. |

### Vp Structure

The `Vp` structure (16 bytes) consists of:

1.  **Scale (8 bytes)**
    *   `vscale[4]` (s16): Scale factors (X, Y, Z, Padding).

2.  **Translation (8 bytes)**
    *   `vtrans[4]` (s16): Translation values (X, Y, Z, Padding).

## YAML Configuration

To define a viewport asset in the configuration, use the `VP` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `VP`. |
| `offset` | Integer | The offset of the viewport structure in the ROM/binary. |
| `symbol` | String | (Optional) The symbol name for the asset. |

Example:
```yaml
- type: VP
  offset: 0x9000
  symbol: my_viewport
```
