# ZPath Bug Analysis: Alternate Header Pathway Count

## Executive Summary

The spot04_scene alternate header (Set_00D590) has a SetPathways command pointing to an address with 2 pathway entries in the ROM. OTRExporter outputs only 1 entry because:

1. **ZPath default numPaths=1**: ZPath resources default to `numPaths=1` (ZPath.cpp line 14)
2. **XML declaration missing**: The SetPathways address for the alternate header is NOT declared in XML
3. **No cross-file resource search**: `FindResource()` only searches the current ZFile, not across files
4. **Doubling mechanism**: When a ZPath resource EXISTS at the SetPathways address, the pathways are doubled in binary output

## ZPath XML Declarations

spot04.xml declares:
```xml
<Path Name="spot04_scenePathList_00030C" Offset="0x030C" NumPaths="3"/>
<Path Name="spot04_scenePathList_00D728" Offset="0xD728" NumPaths="2"/>
```

spot00.xml declares:
```xml
<Path Name="spot00_scenePathList_011AB4" Offset="0x11AB4" NumPaths="2"/>
```

Key points:
- NumPaths is OPTIONAL in XML (defaults to "1")
- These paths are declared in the PRIMARY header's XML, not alternate headers
- Alternate headers' pathway addresses are often NOT in the XML

## ZPath Class Implementation

### Constructor (ZPath.cpp line 12-16)
```cpp
ZPath::ZPath(ZFile* nParent) : ZResource(nParent)
{
    numPaths = 1;  // HARDCODED DEFAULT
    RegisterOptionalAttribute("NumPaths", "1");
}
```

### ParseXML (ZPath.cpp line 18-31)
- Extracts NumPaths from XML attribute
- Default is "1" if not specified
- No mechanism to auto-detect from ROM data

### ParseRawData (ZPath.cpp line 33-51)
```cpp
void ZPath::ParseRawData()
{
    pathways.reserve(numPaths);  // Allocates ONLY numPaths slots
    for (size_t pathIndex = 0; pathIndex < numPaths; pathIndex++)
    {
        PathwayEntry path(parent);
        path.ExtractFromFile(currentPtr);

        if (path.GetListAddress() == 0)
            break;  // Early exit if 0 found

        currentPtr += path.GetRawDataSize();
        pathways.push_back(path);
    }
}
```

**Critical**: This respects the numPaths limit. If numPaths=1 (default), it only reads 1 pathway entry, even if more exist in the ROM.

## SetPathways Command Processing

### ParseRawDataLate (SetPathways.cpp line 23-40)

```cpp
void SetPathways::ParseRawDataLate()
{
    // MM only: infer numPaths from neighbor
    if (Globals::Instance->game == ZGame::MM_RETAIL)
    {
        auto numPaths = zRoom->parent->GetDeclarationSizeFromNeighbor(segmentOffset) / 8;
        pathwayList.SetNumPaths(numPaths);
    }

    // OTR MODE: Try to find ZPath resource
    if (Globals::Instance->otrMode)
    {
        auto zPath = (ZPath*)parent->FindResource(segmentOffset);

        if (zPath != nullptr)
            pathwayList = *zPath;  // COPIES the ZPath including its numPaths
    }

    pathwayList.ExtractFromFile(segmentOffset);  // ParseRawData respects numPaths
}
```

### Flow for alternate header with unique pathway address:

1. `parent->FindResource(segmentOffset)` called with the alt header's pathway address
2. Address not in XML, so no ZPath resource exists
3. `FindResource()` returns nullptr
4. `pathwayList` retains default numPaths=1
5. `pathwayList.ExtractFromFile()` calls ParseRawData()
6. ParseRawData() only loops once (numPaths=1), reads 1 entry
7. Even though ROM has 2+ entries, only 1 is captured

## FindResource Implementation (ZFile.cpp)

```cpp
ZResource* ZFile::FindResource(offset_t rawDataIndex)
{
    for (ZResource* res : resources)  // Only searches this ZFile's resources
    {
        if (res->GetRawDataIndex() == rawDataIndex)
            return res;
    }
    return nullptr;  // NOT FOUND if not in current file
}
```

Each scene file is a separate ZFile. Alternate headers are in the SAME ZFile as the primary, but the pathway address may not be declared in that file's XML resources.

## The Doubling Mechanism

### OTRExporter RoomExporter.cpp (SetPathways case, lines 503-524)
```cpp
case RoomCommand::SetPathways:
{
    SetPathways* cmdSetPathways = (SetPathways*)cmd;

    writer->Write((uint32_t)cmdSetPathways->pathwayList.pathways.size());

    for (size_t i = 0; i < cmdSetPathways->pathwayList.pathways.size(); i++)
    {
        // Write reference path
        writer->Write(path);

        // Create companion file with ENTIRE pathwayList
        OTRExporter_Path pathExp;
        pathExp.Save(&cmdSetPathways->pathwayList, outPath, &pathWriter);
        AddFile(path, pathStream->ToVector());
    }
}
```

### PathExporter.cpp Save function (lines 5-27)
```cpp
void OTRExporter_Path::Save(ZResource* res, ...)
{
    ZPath* path = (ZPath*)res;

    writer->Write((uint32_t)path->pathways.size());

    for (size_t k = 0; k < path->pathways.size(); k++)
    {
        writer->Write((uint32_t)path->pathways[k].points.size());
        // Write point coordinates
    }
}
```

The "doubling" occurs because: for EACH pathway reference in the command, OTRExporter creates a companion file containing ALL pathways. So the binary output has the full pathway list repeated once per reference. For scenes with >1 pathway, this creates duplicate data.

## The spot04 Edge Case

### Why spot04 fails (others pass):

| Scene | Status | Reason |
|-------|--------|--------|
| spot00 | PASS | Alternate headers share primary's pathway address (already in XML) |
| spot03 | PASS | Same — shared pathway address |
| spot04 | FAIL | Alt header (Set_00D590) has UNIQUE pathway address NOT in XML with >1 entries |

### Why it can't be in XML:
- Alternate headers are identified by SetAlternateHeaders command
- Their addresses are found dynamically by reading the ROM
- The XML is static; alternate headers are dynamic
- ZAPDTR has no mechanism to add ZPath resources for dynamically-discovered addresses

## Detection Approaches for Torch

### A: Track processed pathway addresses
During primary header processing, record which pathway list ROM addresses were processed (with their counts). When an alt header encounters an address NOT in this set, limit to numPaths=1. This exactly matches OTRExporter's behavior where FindResource returns nullptr.

**Implementation**: Add a `std::unordered_set<uint32_t> processedPathwayAddrs` to the scene factory. Primary header's SetPathways adds its address. Alt header's SetPathways checks the set.

### B: Check YAML declarations
The zapd_to_torch.py conversion creates Path entries in the YAML from XML declarations. If the alt header's pathway address matches a YAML-declared path, use its count. If not, default to 1.

**Implementation**: Check if `ResolvePointer(cmdArg2)` finds a pre-existing node. If so, the pathway was declared in YAML (from XML). If not, it's a unique alt header pathway → limit to 1.

### C: Accept as known limitation
Only 1 scene (spot04) is affected. The 2 failures (PathwayList + Set) are well-understood.

## Recommended Approach: B (Check YAML declarations)

This is the most accurate because it mirrors OTRExporter's `FindResource` behavior — both check for pre-existing resource declarations. The YAML declarations come from the same XML that ZAPDTR uses to create ZPath resources.

The check is simple:
```cpp
auto existingPath = ResolvePointer(cmdArg2);
bool hasPreExistingResource = !existingPath.empty();

if (!hasPreExistingResource && isAltHeader && pathways.size() > 1) {
    pathways.erase(pathways.begin() + 1, pathways.end());
}
bool doubled = hasPreExistingResource && (pathways.size() > 1);
```

## Files Referenced
- `Shipwright/ZAPDTR/ZAPD/ZPath.cpp` — ZPath class, numPaths default
- `Shipwright/ZAPDTR/ZAPD/ZPath.h` — Class definition
- `Shipwright/ZAPDTR/ZAPD/ZRoom/Commands/SetPathways.cpp` — ParseRawDataLate
- `Shipwright/ZAPDTR/ZAPD/ZFile.cpp` — FindResource (single-file search)
- `Shipwright/OTRExporter/OTRExporter/RoomExporter.cpp` — SetPathways case
- `Shipwright/OTRExporter/OTRExporter/PathExporter.cpp` — Save function
- `Torch/src/factories/oot/OoTSceneFactory.cpp` — Pathway scanning
