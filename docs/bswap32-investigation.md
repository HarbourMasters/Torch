# BSWAP32 Investigation: Cartridge.cpp CRC Reading

## Summary

`Cartridge.cpp` applies `BSWAP32` to the ROM CRC after `BinaryReader` has
already correctly decoded it, producing a byte-swapped value. Starship's
`SF64_VER_*` constants were defined to match this swapped value, so the system
works by accident (two wrongs making a right). Removing the `BSWAP32` produces
the correct standard N64 CRC, and Starship will need to update their constants
to match.

## The bug

```cpp
// Cartridge.cpp:10
this->gRomCRC = BSWAP32(reader.ReadUInt32());
```

`BinaryReader` is set to Big endian. On a little-endian host, `ReadUInt32()`
reads 4 bytes and byte-swaps them to produce the correct native uint32 value.
The extra `BSWAP32` then swaps it again, corrupting it.

## How we confirmed the constants are wrong

We used `calculate_crcs` from the sf64 decomp project
([sonicdcer/sf64 comptool.py](https://github.com/sonicdcer/sf64/blob/master/tools/comptool.py))
-- the same CRC algorithm used by the N64 SDK (CIC-NUS-6102, seed
`0xF8CA4DDC`). We MIO0-decompressed the actual SF64 JP and US ROMs using the
decomp project's mio0 tool and ran the CRC calculation on the results:

```
Decompressed JP ROM standard CRC1: 0xE1D32837
SF64_VER_JP (Starship Engine.h):   0x3728D3E1  =  BSWAP32(0xE1D32837)
```

```
Decompressed US v1.0 standard CRC1: 0xA94C9276
O2R version stores:                  0x76924CA9  =  BSWAP32(0xA94C9276)
```

Every `SF64_VER_*` constant in Starship is `BSWAP32(standard CRC)` rather than
the standard CRC itself.

For comparison, SoH (OoT) defines its version constants as the standard CRC.
OTRExporter reads the CRC with `BitConverter::ToUInt32BE(romData, 0x10)` (direct
byte-shift, no BinaryReader, no BSWAP32), producing the correct value. SoH
validates O2R versions against a strict whitelist of these constants in
`GameVersions.h`, so a byte-swapped value would be rejected at startup.

## Why it worked for Starship

Two bugs cancel out:

1. `BSWAP32` in `Cartridge.cpp` stores `BSWAP32(standard CRC)` as `gRomCRC`
2. `SF64_VER_*` constants are defined as `BSWAP32(standard CRC)`

The version file write (Big-endian `BinaryWriter`) and LUS read (Big-endian
`BinaryReader`) round-trip preserves the value, so LUS stores the same swapped
value that `HasVersion()` checks against. Everything matches, but everything is
wrong.

## Fix

**In Torch**: Remove `BSWAP32` from `Cartridge.cpp:10`:

```cpp
// Before:
this->gRomCRC = BSWAP32(reader.ReadUInt32());

// After:
this->gRomCRC = reader.ReadUInt32();
```

**In Starship** (when updating Torch submodule): Update `SF64_VER_*` constants
in `Engine.h` to the standard CRC values. For example:

```cpp
// Before (byte-swapped):
SF64_VER_JP = 0x3728D3E1

// After (standard CRC):
SF64_VER_JP = 0xE1D32837
```
