# portVersion writeup and fix plan

## How portVersion works in Shipwright

### What it is
A 7-byte binary file at the root of every O2R archive. Contains the SoH port version
(e.g. 9.2.0) used for compatibility checking at runtime.

### Binary format
```
Byte 0:    uint8_t  endianness (0x00=Big, 0x01=Little)
Bytes 1-2: uint16_t major (Big Endian)
Bytes 3-4: uint16_t minor (Big Endian)
Bytes 5-6: uint16_t patch (Big Endian)
```
Total: 7 bytes. NOT a Torch resource — no 64-byte header. Raw binary in the zip.

Endianness values: Little=0, Big=1 (same in Ship:: and Torch:: namespaces).

Reference O2R: `01 00 09 00 02 00 00` → endianness=Big(1), version 9.2.0

### Runtime reading (SoH OTRGlobals.cpp:1415-1434)
```cpp
Ship::Endianness endianness = (Ship::Endianness)reader->ReadUByte(); // reads 0x01 = Big
reader->SetEndianness(endianness);  // reader now reads uint16s as Big Endian
version.major = reader->ReadUInt16();  // 0x0009 = 9
version.minor = reader->ReadUInt16();  // 0x0002 = 2
version.patch = reader->ReadUInt16();  // 0x0000 = 0
```
The endianness byte tells the reader how the subsequent uint16 values were encoded.
OTRExporter always writes Big Endian — the endianness flag is hardcoded to
`(uint8_t)Endianness::Big` (Main.cpp line 96), not derived from the platform.
The flag exists because the reader is generic (supports any endianness), but in
practice OTRExporter always stamps Big and writes BE data.

### Generation (OTRExporter)
- `OTRExporter/Main.cpp` lines 41-160
- Version string passed via `--portVer` CLI arg (from CMake `${CMAKE_PROJECT_VERSION}`)
- Writes endianness byte + 3 × uint16_t BE to a MemoryStream
- Added to archive via `archive->AddFile("portVersion", data)`

### Runtime consumption (SoH)
- `soh/OTRGlobals.cpp` `ReadPortVersionFromOTR()` reads and parses the 7 bytes
- Compared against `gBuildVersionMajor/Minor/Patch` (compiled from CMake version)
- **Port archive (soh.o2r)**: mismatch → "outdated" popup, blocks launch
- **Game archives (oot.o2r)**: mismatch → forces re-extraction from ROM
- **Save files**: version stored in saves, mismatch → `.bak` rename

### Relationship to "version" asset
- `version` = 5 bytes: endianness(1) + ROM CRC32(4). Identifies which ROM was used.
- `portVersion` = 7 bytes: endianness(1) + major/minor/patch(6). Identifies SoH build.
- Both are raw binary at archive root (no resource header).

## Why portVersion is missing from generated O2R

### Root cause
1. `test_assets.py` line 213 runs: `torch o2r -s dir -d out rom` — no `-u/--version` flag
2. `Companion::gVersion` stays empty (default)
3. `Companion.cpp` line 1438: `if (!this->gVersion.empty())` → skips writing portVersion

### Secondary issue: format mismatch
Torch's `ParseVersionString` (line 1898) writes 6 bytes (3 × uint16_t BE).
OTRExporter writes 7 bytes (1 endianness byte + 3 × uint16_t BE).

## Fix

### 1. Pass version to Torch in test_assets.py
Add `-u 9.2.0` to the torch command line (matching reference O2R build version).

### 2. Fix ParseVersionString to include endianness byte
Add the endianness byte (0x01 for LE) before the version numbers:
```cpp
std::vector<char> Companion::ParseVersionString(const std::string& version) {
    uint16_t major = 0, minor = 0, patch = 0;
    std::sscanf(version.c_str(), "%hu.%hu.%hu", &major, &minor, &patch);
    auto wv = LUS::BinaryWriter();
    wv.SetEndianness(Torch::Endianness::Big);
    wv.Write(static_cast<uint8_t>(0x01));  // endianness flag (LE)
    wv.Write(major);
    wv.Write(minor);
    wv.Write(patch);
    wv.Close();
    return wv.ToVector();
}
```

### Files to modify
- `src/Companion.cpp` — add endianness byte to ParseVersionString
- `soh/tools/test_assets.py` — pass `-u 9.2.0` to torch command

### Verification
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64`
- Expected: 35,386/35,386 (100%)
