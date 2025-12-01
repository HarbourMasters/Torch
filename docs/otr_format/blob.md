@page otr_blob Blob Resource
# Blob Resource (OBLB)

If the Resource Type is `OBLB` (`0x4F424C42`), the header is immediately followed by the blob definition. This resource type is used for generic binary data that doesn't fit into other specific categories.

## Blob Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 4 | u32 | Data Size |
| 0x44 | ... | [u8] | Raw Blob Data |

## YAML Configuration

To define a blob asset in the configuration, use the `BLOB` type.

| Field | Type | Description |
| :--- | :--- | :--- |
| `type` | String | Must be `BLOB`. |
| `offset` | Integer | The offset of the blob data in the ROM/binary. |
| `size` | Integer | The size of the blob data in bytes. |
| `symbol` | String | (Optional) The symbol name for the asset. |

Example:
```yaml
- type: BLOB
  offset: 0x1000
  size: 0x200
  symbol: my_custom_blob
```
