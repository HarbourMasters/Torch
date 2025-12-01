@page otr_format OTR File Format Specification
# OTR File Format Specification

## Overview
The OTR (Ocarina of Time Recompiled / Open Texture Resource) format is a custom archive format used to store game assets. It is primarily a ZIP archive containing individual asset files with specific binary headers.

## Container Format
The `.otr` or `.o2r` file is a standard **ZIP archive**. Files inside the archive can be named arbitrarily, but typically follow a directory structure mirroring the original game's asset paths.

## Asset Structure
Each file within the ZIP archive begins with a standard 64-byte header, followed by resource-specific data.

### Standard Header (64 bytes)

| Offset | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x00 | 1 | int8 | Byte Order |
| 0x01 | 1 | bool | Is Custom Asset (0 = Original, 1 = Custom) |
| 0x02 | 2 | - | *Padding* |
| 0x04 | 4 | u32 | Resource Type (Magic Number) |
| 0x08 | 4 | u32 | Version |
| 0x0C | 8 | u64 | Unique Asset ID |
| 0x14 | 44 | - | *Reserved / Padding* |

**Note:** All multi-byte integers are stored in **Little Endian** format.

### Resource Types
The `Resource Type` field identifies the kind of data stored in the asset.

| Magic Value | ASCII | Description |
| :--- | :--- | :--- |
| `0x4F444C54` | ODLT | [Display List](@subpage otr_display_list) |
| `0x46669697` | LGTS | [Light](@subpage otr_light) |
| `0x4F4D5458` | OMTX | [Matrix](@subpage otr_matrix) |
| `0x4F464C54` | OFLT | [Float](@subpage otr_float) |
| `0x56433346` | VC3F | [Vec3f](@subpage otr_vec3f) |
| `0x56433353` | VC3S | [Vec3s](@subpage otr_vec3s) |
| `0x4F424C42` | OBLB | [Blob](@subpage otr_blob) |
| `0x4F544558` | OTEX | [Texture](@subpage otr_texture) |
| `0x4F565458` | OVTX | [Vertex](@subpage otr_vertex) |
| `0x4F565054` | OVPT | [Viewport](@subpage otr_viewport) |
| `0x41415252` | AARR | [Asset Array](@subpage otr_asset_array) |
| `0x47415252` | GARR | [Generic Array](@subpage otr_generic_array) |