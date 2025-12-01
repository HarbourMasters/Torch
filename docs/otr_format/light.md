@page otr_light Light Resource
# Light Resource (LGTS)

If the Resource Type is `LGTS` (`0x46669697`), the header is immediately followed by the light definition.

## Light Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 24 | Lights1 | The Lights1 structure containing ambient and diffuse light data. |

### Lights1 Structure

The `Lights1` structure (24 bytes) consists of:

1.  **Ambient (8 bytes)**
    *   `col[3]` (u8): Ambient RGB color.
    *   `pad1` (i8): Padding.
    *   `colc[3]` (u8): Copy of ambient RGB color.
    *   `pad2` (i8): Padding.

2.  **Diffuse (16 bytes)**
    *   `col[3]` (u8): Diffuse RGB color.
    *   `pad1` (i8): Padding.
    *   `colc[3]` (u8): Copy of diffuse RGB color.
    *   `pad2` (i8): Padding.
    *   `dir[3]` (i8): Light direction (normalized).
    *   `pad3` (i8): Padding.

## YAML Configuration

To define a light asset in the configuration, use the `LIGHTS` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `LIGHTS`. |
| `offset` | Integer | The offset of the light structure in the ROM/binary. |
| `symbol` | String | (Optional) The symbol name for the asset. |

Example:
```yaml
- type: LIGHTS
  offset: 0x4000
  symbol: my_light
```
