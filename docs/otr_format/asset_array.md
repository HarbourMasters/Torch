@page otr_asset_array Asset Array Resource
# Asset Array Resource (AARR)

If the Resource Type is `AARR` (`0x41415252`), the header is immediately followed by the asset array definition. This resource type is used to store an array of pointers to other assets.

## Asset Array Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 4 | u32 | Count |
| 0x44 | ... | [u64] | Array of Asset Hashes |

Each entry in the array is a 64-bit hash of the referenced asset's path. If the pointer in the original binary was NULL, the hash is 0.

## YAML Configuration

To define an asset array in the configuration, use the `ASSET_ARRAY` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `ASSET_ARRAY`. |
| `offset` | Integer | The offset of the array in the ROM/binary. |
| `count` | Integer | The number of pointers in the array. |
| `assetType` | String | The C type of the assets pointed to (e.g., `Gfx`, `Vtx`). |
| `factoryType` | String | The factory type to use for the referenced assets (e.g., `GFX`, `VTX`). |
| `symbol` | String | (Optional) The symbol name for the asset. |

Example:
```yaml
- type: ASSET_ARRAY
  offset: 0xA000
  count: 5
  assetType: Gfx
  factoryType: GFX
  symbol: my_display_list_array
```
