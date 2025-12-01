@page otr_texture Texture Resource
# Texture Resource (OTEX)

If the Resource Type is `OTEX` (`0x4F544558`), the header is immediately followed by the texture definition.

## Texture Header

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x40 | 4 | u32 | Texture Type ID |
| 0x44 | 4 | u32 | Width |
| 0x48 | 4 | u32 | Height |
| 0x4C | 4 | u32 | Data Size |
| 0x50 | ... | [u8] | Raw Texture Data |

## Texture Types

| ID | Name | Bits Per Pixel | Description |
| :--- | :--- | :--- | :--- |
| 0 | Error | - | Invalid / Error |
| 1 | RGBA32bpp | 32 | Red, Green, Blue, Alpha (8 bits each) |
| 2 | RGBA16bpp | 16 | RGBA 5-5-5-1 |
| 3 | Palette4bpp | 4 | 4-bit Color Index (CI4) |
| 4 | Palette8bpp | 8 | 8-bit Color Index (CI8) |
| 5 | Grayscale4bpp | 4 | 4-bit Intensity (I4) |
| 6 | Grayscale8bpp | 8 | 8-bit Intensity (I8) |
| 7 | GrayscaleAlpha4bpp | 4 | 4-bit Intensity with Alpha (IA4) |
| 8 | GrayscaleAlpha8bpp | 8 | 8-bit Intensity with Alpha (IA8) |
| 9 | GrayscaleAlpha16bpp | 16 | 16-bit Intensity with Alpha (IA16) |
| 10 | GrayscaleAlpha1bpp | 1 | 1-bit Black/White (I1) |
| 11 | TLUT | - | Texture Look-Up Table (Palette) |

**Note:** When a `TLUT` is exported to an OTR file, its type is converted to `RGBA16bpp` (ID 2).

## YAML Configuration

When defining textures in the asset configuration, the following format strings are supported:

| Format String | Texture Type | Bit Depth |
| :--- | :--- | :--- |
| `rgba16` | RGBA16bpp | 16 |
| `rgba32` | RGBA32bpp | 32 |
| `ci4` | Palette4bpp | 4 |
| `ci8` | Palette8bpp | 8 |
| `i4` | Grayscale4bpp | 4 |
| `i8` | Grayscale8bpp | 8 |
| `ia1` | GrayscaleAlpha1bpp | 1 |
| `ia4` | GrayscaleAlpha4bpp | 4 |
| `ia8` | GrayscaleAlpha8bpp | 8 |
| `ia16` | GrayscaleAlpha16bpp | 16 |
| `tlut` | TLUT | 16 |

### Paletted Textures (CI4 / CI8)

For paletted textures (`ci4` or `ci8`), you can optionally define the accompanying TLUT within the same entry. If `tlut` (offset) and `colors` (count) are provided, Torch will automatically generate a separate TLUT asset.

You can also specify `tlut_symbol` to explicitly name the generated TLUT asset. If omitted, it defaults to the texture symbol with `_tlut` appended.

Example:
```yaml
- type: TEXTURE
  format: ci8
  width: 32
  height: 32
  offset: 0x12345
  tlut: 0x12545
  colors: 256
  tlut_symbol: my_custom_tlut_name # Optional
```
This will create the texture asset and a separate TLUT asset.
