@page otr_display_list Display List Resource
# Display List Resource (ODLT)

If the Resource Type is `ODLT` (`0x4F444C54`), the header is immediately followed by the display list definition.

## Display List Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 1 | int8 | GBI Version (F3D, F3DEX, F3DEX2, etc.) |
| 0x41 | 7 | - | *Padding* |
| 0x48 | 8 | u64 | Resource Hash |
| 0x50 | ... | [u64] | Display List Commands (Gfx) |

## Display List Commands

The display list commands are stored as a sequence of 64-bit words (Gfx). The format of these commands depends on the GBI version specified in the header.

When exporting to OTR, certain commands are modified to use resource hashes instead of memory addresses:

- `G_VTX`: Vertex buffer addresses are replaced with hashes of the vertex resource.
- `G_DL`: Display list branch/link addresses are replaced with hashes of the target display list resource.
- `G_MOVEMEM`: Memory addresses (e.g., for lights) are replaced with hashes of the referenced resource.
- `G_SETTIMG`: Texture addresses are replaced with hashes of the texture resource.
- `G_MTX`: Matrix addresses are replaced with hashes of the matrix resource.

## YAML Configuration

To define a display list asset in the configuration, use the `GFX` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `GFX`. |
| `offset` | Integer | The offset of the display list in the ROM/binary. |
| `symbol` | String | (Optional) The symbol name for the asset. |
| `count` | Integer | (Optional) The number of Gfx commands to parse. If not specified, parsing continues until `G_ENDDL`. |

Example:
```yaml
- type: GFX
  offset: 0x2000
  symbol: my_display_list
```
