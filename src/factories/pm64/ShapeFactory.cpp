#include "ShapeFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include <unordered_set>
#include <sstream>
#include <cstring>
#include "n64/CommandMacros.h"
#include "factories/DisplayListOverrides.h"
#include "n64/gbi-otr.h"
#include "strhash64/StrHash64.h"

// PM64 shape file structures (matching model.h):
// ShapeFileHeader (0x20 bytes):
//   0x00: root (ModelNode*)
//   0x04: vertexTable (Vtx_t*)
//   0x08: modelNames (char**)
//   0x0C: colliderNames (char**)
//   0x10: zoneNames (char**)
//   0x14: pad[0xC]
//
// ModelNode (0x14 bytes):
//   0x00: type (s32)
//   0x04: displayData (ModelDisplayData*)
//   0x08: numProperties (s32)
//   0x0C: propertyList (ModelNodeProperty*)
//   0x10: groupData (ModelGroupData*)
//
// ModelGroupData (0x14 bytes):
//   0x00: transformMatrix (Mtx*)
//   0x04: lightingGroup (Lightsn*)
//   0x08: numLights (s32)
//   0x0C: numChildren (s32)
//   0x10: childList (ModelNode**)
//
// ModelDisplayData (0x08 bytes):
//   0x00: displayList (Gfx*)
//   0x04: unk_04 (4 bytes)
//
// ModelNodeProperty (0x0C bytes):
//   0x00: key (s32)
//   0x04: dataType (s32)
//   0x08: data (union: s32/f32/void*)

// Base address used for N64 virtual address to offset conversion
// This is computed from the header's root pointer assuming root is at offset 0x20
static uint32_t gShapeBaseAddr = 0;

// Vertex table offset in shape file - used for converting G_VTX offsets to vertex-table-relative
static uint32_t gVertexTableOffset = 0;

// Track visited offsets to prevent infinite recursion from cycles and to exclude from vertex byte-swapping
static std::unordered_set<uint32_t> gVisitedNodes;
static std::unordered_set<uint32_t> gVisitedGroups;
static std::unordered_set<uint32_t> gVisitedMatrices;
static std::unordered_set<uint32_t> gVisitedDisplayLists;
static std::unordered_set<uint32_t> gVisitedDisplayData;
static std::unordered_set<uint32_t> gVisitedProperties;

// Collected display lists during parsing
static std::vector<PM64DisplayListInfo>* gCollectedDisplayLists = nullptr;

// Convert N64 virtual address to file offset
static uint32_t N64AddrToOffset(uint32_t addr) {
    if (addr == 0)
        return 0;

    // Check if it looks like an N64 virtual address (segment in high byte)
    if (addr >= 0x80000000) {
        // It's an N64 address - convert using base
        if (gShapeBaseAddr == 0) {
            // Fallback: mask off the segment and use as offset
            return addr & 0x00FFFFFF;
        }
        if (addr >= gShapeBaseAddr) {
            return addr - gShapeBaseAddr;
        }
        // Address is below base - might be in a different segment, try masking
        return addr & 0x00FFFFFF;
    }

    // Small value - assume it's already a file offset
    return addr;
}

static bool IsValidOffset(uint32_t offset, size_t size) {
    return offset > 0 && offset < size;
}

// F3DEX2 GBI opcodes used in shape display lists
#define F3DEX2_G_ENDDL 0xDF
#define F3DEX2_G_VTX 0x01
#define F3DEX2_G_DL 0xDE
#define F3DEX2_G_SETTIMG 0xFD

// Check if an opcode is a valid F3DEX2 GBI command.
// Valid ranges: 0x00-0x07 (geometry), 0xD7-0xDF (matrix/mode), 0xE4-0xFF (RDP).
static bool IsValidF3DEX2Opcode(uint8_t opcode) {
    if (opcode <= 0x07)
        return true; // G_NOOP..G_QUAD
    if (opcode >= 0xD7 && opcode <= 0xDF)
        return true; // G_TEXTURE..G_ENDDL
    if (opcode >= 0xE4)
        return true; // G_TEXRECT..G_SETCIMG
    return false;
}

// Byte-swap display list commands, convert embedded N64 addresses to file offsets,
// and collect the display list for separate resource export
static void ByteSwapDisplayList(uint8_t* data, uint32_t offset, size_t size) {
    if (!IsValidOffset(offset, size - 8))
        return;
    if (gVisitedDisplayLists.count(offset))
        return; // Already processed
    gVisitedDisplayLists.insert(offset);

    uint8_t* ptr = data + offset;
    uint8_t* endPtr = data + size;

    // Collect this display list's commands
    PM64DisplayListInfo dlInfo;
    dlInfo.offset = offset;

    while (ptr + 8 <= endPtr) {
        uint32_t* words = reinterpret_cast<uint32_t*>(ptr);

        // Read big-endian words
        uint32_t w0 = BSWAP32(words[0]);
        uint32_t w1 = BSWAP32(words[1]);

        uint8_t opcode = (w0 >> 24) & 0xFF;

        // Stop if we hit a non-F3DEX2 opcode — we've overrun past the display list
        // into adjacent data (e.g., string data, vertex data, padding).
        if (!IsValidF3DEX2Opcode(opcode)) {
            SPDLOG_WARN("DL at 0x{:X}: invalid opcode 0x{:02X} at offset 0x{:X}, stopping", offset, opcode,
                        (uint32_t)(ptr - data));
            break;
        }

        // Handle G_VTX - convert vertex address to vertex-table-relative offset
        if (opcode == F3DEX2_G_VTX) {
            // w1 contains the N64 vertex address - convert to file offset first
            uint32_t vtxFileOffset = N64AddrToOffset(w1);
            // Then convert to vertex-table-relative byte offset (stride is 16, same as sizeof(Vtx))
            if (gVertexTableOffset > 0 && vtxFileOffset >= gVertexTableOffset) {
                w1 = vtxFileOffset - gVertexTableOffset;
            } else {
                w1 = vtxFileOffset;
            }
            if (Companion::Instance->GetConfig().gbi.useFloats) {
                // N64 Vtx is 16 bytes, float Vtx is 24 bytes — rescale the byte offset
                uint32_t vtxIndex = w1 / 16;
                w1 = vtxIndex * 24;
            }
        }

        // Handle G_SETTIMG - convert texture address to file offset
        // Note: Shape textures may be loaded separately, but we convert anyway for safety
        if (opcode == F3DEX2_G_SETTIMG) {
            // w1 contains the N64 texture address
            w1 = N64AddrToOffset(w1);
        }

        // Handle G_DL - convert display list address to file offset and recurse
        if (opcode == F3DEX2_G_DL) {
            uint32_t dlOffset = N64AddrToOffset(w1);
            w1 = dlOffset;
            // Recursively process the referenced display list
            ByteSwapDisplayList(data, dlOffset, size);
        }

        // Write the byte-swapped (now little-endian) words back
        words[0] = w0;
        words[1] = w1;

        // Collect the command for OTR export
        dlInfo.commands.push_back(w0);
        dlInfo.commands.push_back(w1);

        // Stop at G_ENDDL
        if (opcode == F3DEX2_G_ENDDL) {
            break;
        }

        ptr += 8; // Move to next Gfx command (8 bytes each)
    }

    // Add to collected display lists
    if (gCollectedDisplayLists && !dlInfo.commands.empty()) {
        gCollectedDisplayLists->push_back(std::move(dlInfo));
    }
}

// Property key for texture names - these store N64 addresses to strings
#define MODEL_PROP_KEY_TEXTURE_NAME 0x5E

static void ByteSwapModelNodeProperty(uint8_t* data, uint32_t offset, size_t size) {
    if (!IsValidOffset(offset, size - 0xC))
        return;

    uint32_t* prop = reinterpret_cast<uint32_t*>(data + offset);
    int32_t key = static_cast<int32_t>(BSWAP32(prop[0]));
    prop[0] = static_cast<uint32_t>(key);
    prop[1] = BSWAP32(prop[1]); // dataType

    // For texture name properties, convert N64 address to file offset
    if (key == MODEL_PROP_KEY_TEXTURE_NAME) {
        uint32_t dataAddr = BSWAP32(prop[2]);
        uint32_t strOffset = N64AddrToOffset(dataAddr);
        prop[2] = strOffset;
    } else {
        prop[2] = BSWAP32(prop[2]); // data (scalar value)
    }
}

static void ByteSwapModelDisplayData(uint8_t* data, uint32_t offset, size_t size) {
    if (!IsValidOffset(offset, size - 0x8))
        return;

    // Track this offset so vertex byte-swapping skips it
    gVisitedDisplayData.insert(offset);

    uint32_t* display = reinterpret_cast<uint32_t*>(data + offset);
    // displayList is a pointer - convert N64 address to offset
    uint32_t dlAddr = BSWAP32(display[0]);
    uint32_t dlOffset = N64AddrToOffset(dlAddr);

    display[0] = dlOffset;
    display[1] = BSWAP32(display[1]); // unk_04

    // Byte-swap the display list commands themselves and collect for export
    if (IsValidOffset(dlOffset, size)) {
        ByteSwapDisplayList(data, dlOffset, size);
    }
}

static void ByteSwapModelGroupData(uint8_t* data, uint32_t offset, size_t size);
static void ByteSwapModelNode(uint8_t* data, uint32_t offset, size_t size);

static void ByteSwapModelGroupData(uint8_t* data, uint32_t offset, size_t size) {
    if (!IsValidOffset(offset, size - 0x14))
        return;
    if (gVisitedGroups.count(offset))
        return; // Already processed
    gVisitedGroups.insert(offset);

    uint32_t* group = reinterpret_cast<uint32_t*>(data + offset);

    // Read and convert N64 addresses to offsets
    uint32_t transformMatrixAddr = BSWAP32(group[0]);
    uint32_t lightingGroupAddr = BSWAP32(group[1]);
    int32_t numLights = static_cast<int32_t>(BSWAP32(group[2]));
    int32_t numChildren = static_cast<int32_t>(BSWAP32(group[3]));
    uint32_t childListAddr = BSWAP32(group[4]);

    // Convert N64 addresses to file offsets
    uint32_t transformMatrix = N64AddrToOffset(transformMatrixAddr);
    uint32_t lightingGroup = N64AddrToOffset(lightingGroupAddr);
    uint32_t childList = N64AddrToOffset(childListAddr);

    group[0] = transformMatrix;
    group[1] = lightingGroup;
    group[2] = static_cast<uint32_t>(numLights);
    group[3] = static_cast<uint32_t>(numChildren);
    group[4] = childList;

    // Byte-swap transform matrix — multiple groups can share the same matrix, only convert once
    if (IsValidOffset(transformMatrix, size - 0x40) && !gVisitedMatrices.count(transformMatrix)) {
        gVisitedMatrices.insert(transformMatrix);
        uint32_t* raw = reinterpret_cast<uint32_t*>(data + transformMatrix);
        for (int i = 0; i < 16; i++) {
            raw[i] = BSWAP32(raw[i]);
        }
        if (Companion::Instance->GetConfig().gbi.useFloats) {
            // Decode interleaved integer/fraction parts to float[4][4]
            int32_t* addr = reinterpret_cast<int32_t*>(raw);
            float matrix[4][4];
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 2; j++) {
                    int32_t int_part = addr[i * 2 + j];
                    uint32_t frac_part = addr[8 + i * 2 + j];
                    matrix[i][j * 2] = (int32_t)((int_part & 0xFFFF0000) | (frac_part >> 16)) / 65536.0f;
                    matrix[i][j * 2 + 1] = (int32_t)((int_part << 16) | (frac_part & 0xFFFF)) / 65536.0f;
                }
            }
            memcpy(raw, matrix, sizeof(matrix));
        }
    }

    // Byte-swap child list and recurse into child nodes
    // Sanity check: numChildren should be reasonable (< 1000)
    if (numChildren > 0 && numChildren < 1000 && IsValidOffset(childList, size - (numChildren * 4))) {
        uint32_t* children = reinterpret_cast<uint32_t*>(data + childList);
        for (int i = 0; i < numChildren; i++) {
            uint32_t childAddr = BSWAP32(children[i]);
            uint32_t childOffset = N64AddrToOffset(childAddr);
            children[i] = childOffset;
            ByteSwapModelNode(data, childOffset, size);
        }
    }
}

static void ByteSwapModelNode(uint8_t* data, uint32_t offset, size_t size) {
    if (!IsValidOffset(offset, size - 0x14))
        return;
    if (gVisitedNodes.count(offset))
        return; // Already processed
    gVisitedNodes.insert(offset);

    uint32_t* node = reinterpret_cast<uint32_t*>(data + offset);

    // Read and convert N64 addresses
    int32_t type = static_cast<int32_t>(BSWAP32(node[0]));
    uint32_t displayDataAddr = BSWAP32(node[1]);
    int32_t numProperties = static_cast<int32_t>(BSWAP32(node[2]));
    uint32_t propertyListAddr = BSWAP32(node[3]);
    uint32_t groupDataAddr = BSWAP32(node[4]);

    // Convert N64 addresses to file offsets
    uint32_t displayData = N64AddrToOffset(displayDataAddr);
    uint32_t propertyList = N64AddrToOffset(propertyListAddr);
    uint32_t groupData = N64AddrToOffset(groupDataAddr);

    node[0] = static_cast<uint32_t>(type);
    node[1] = displayData;
    node[2] = static_cast<uint32_t>(numProperties);
    node[3] = propertyList;
    node[4] = groupData;

    // Byte-swap display data
    if (IsValidOffset(displayData, size)) {
        ByteSwapModelDisplayData(data, displayData, size);
    }

    // Byte-swap properties and track their offsets for vertex byte-swap exclusion
    if (numProperties > 0 && IsValidOffset(propertyList, size)) {
        gVisitedProperties.insert(propertyList);
        for (int i = 0; i < numProperties; i++) {
            ByteSwapModelNodeProperty(data, propertyList + (i * 0xC), size);
        }
    }

    // Byte-swap group data (which recursively handles children)
    if (IsValidOffset(groupData, size)) {
        ByteSwapModelGroupData(data, groupData, size);
    }
}

// Find the ROOT node (type=7) by scanning the shape data
// Returns the file offset of the ROOT node, or 0 if not found.
//
// Some shapes (e.g. hos_03) contain a dummy/unused node with type=7 that appears
// earlier in the file than the real root and passes the same surface-level
// validation. To disambiguate, we collect ALL type=7 candidates and prefer
// the one whose implied base addres equals the standard PM64 shape base 0x80210000.
static uint32_t FindRootNodeOffset(uint8_t* data, size_t size) {
    uint32_t headerRootAddr = BSWAP32(*reinterpret_cast<uint32_t*>(data));

    uint32_t firstValid = 0;        // fallback: behaviour before the disambiguation
    uint32_t bestExact = 0;         // implied base == 0x80210000 (the known PM64 base)
    uint32_t bestAligned = 0;       // implied base aligned to 0x10000

    for (uint32_t offset = 0x20; offset < size - 0x14; offset += 4) {
        int32_t type = static_cast<int32_t>(BSWAP32(*reinterpret_cast<uint32_t*>(data + offset)));

        if (type != 7) // SHAPE_TYPE_ROOT
            continue;

        uint32_t displayAddr = BSWAP32(*reinterpret_cast<uint32_t*>(data + offset + 0x04));
        int32_t numProps = static_cast<int32_t>(BSWAP32(*reinterpret_cast<uint32_t*>(data + offset + 0x08)));
        uint32_t groupAddr = BSWAP32(*reinterpret_cast<uint32_t*>(data + offset + 0x10));

        bool valid = (displayAddr == 0 || displayAddr > 0x80000000);
        valid = valid && (groupAddr == 0 || groupAddr > 0x80000000);
        valid = valid && (numProps >= 0 && numProps <= 100);
        if (!valid)
            continue;

        if (firstValid == 0)
            firstValid = offset;

        // Skip the implied-base check when header[0] doesn't look like a valid
        // N64 vaddr
        if (headerRootAddr <= 0x80000000 || headerRootAddr <= offset)
            continue;

        uint32_t impliedBase = headerRootAddr - offset;
        if (impliedBase == 0x80210000) {
            bestExact = offset;
            break; // perfect match — stop scanning
        }
        if (bestAligned == 0 && (impliedBase & 0xFFFF) == 0) {
            bestAligned = offset;
        }
    }

    if (bestExact != 0)
        return bestExact;
    if (bestAligned != 0)
        return bestAligned;
    if (firstValid != 0)
        return firstValid;

    SPDLOG_WARN("Could not find ROOT node in shape data");
    return 0;
}

static void ByteSwapShapeData(uint8_t* data, size_t size, std::vector<PM64DisplayListInfo>& collectedDLs,
                              uint32_t& outVtxTableOffset, uint32_t& outVtxDataSize) {
    outVtxTableOffset = 0;
    outVtxDataSize = 0;

    if (size < 0x20) {
        SPDLOG_WARN("Shape data too small: {}", size);
        return;
    }

    // Clear visited sets for this shape file
    gVisitedNodes.clear();
    gVisitedGroups.clear();
    gVisitedMatrices.clear();
    gVisitedDisplayLists.clear();
    gVisitedDisplayData.clear();
    gVisitedProperties.clear();

    // Set up collection target
    gCollectedDisplayLists = &collectedDLs;

    // Read header values (N64 virtual addresses, big-endian)
    uint32_t* header = reinterpret_cast<uint32_t*>(data);
    uint32_t rootAddr = BSWAP32(header[0]);
    uint32_t vertexTableAddr = BSWAP32(header[1]);
    uint32_t modelNamesAddr = BSWAP32(header[2]);
    uint32_t colliderNamesAddr = BSWAP32(header[3]);
    uint32_t zoneNamesAddr = BSWAP32(header[4]);

    // Find the ROOT node by scanning the data to compute correct base address
    uint32_t rootFileOffset = FindRootNodeOffset(data, size);

    if (rootFileOffset > 0 && rootAddr > 0x80000000) {
        // Compute base from actual ROOT node location: base = rootAddr - rootFileOffset
        gShapeBaseAddr = rootAddr - rootFileOffset;
    } else {
        // Fallback to known PM64 base (verified across all tested shapes)
        gShapeBaseAddr = 0x80210000;
        SPDLOG_WARN("Using fallback base address: 0x{:X}", gShapeBaseAddr);
    }

    // Convert N64 addresses to file offsets
    uint32_t root = N64AddrToOffset(rootAddr);
    uint32_t vertexTable = N64AddrToOffset(vertexTableAddr);
    uint32_t modelNames = N64AddrToOffset(modelNamesAddr);
    uint32_t colliderNames = N64AddrToOffset(colliderNamesAddr);
    uint32_t zoneNames = N64AddrToOffset(zoneNamesAddr);

    // Validate root offset is within bounds
    if (root >= size) {
        SPDLOG_ERROR("Root offset 0x{:X} exceeds file size {}!", root, size);
        return;
    }

    // Set vertex table offset global BEFORE processing display lists
    // This allows ByteSwapDisplayList to convert G_VTX offsets to vertex-table-relative
    gVertexTableOffset = vertexTable;
    outVtxTableOffset = vertexTable;

    // Store converted offsets back to header
    header[0] = root;
    header[1] = vertexTable;
    header[2] = modelNames;
    header[3] = colliderNames;
    header[4] = zoneNames;

    // Byte-swap the root ModelNode tree recursively
    if (IsValidOffset(root, size)) {
        ByteSwapModelNode(data, root, size);
    } else {
        SPDLOG_WARN("Root offset 0x{:X} is invalid for size {}", root, size);
    }

    // Byte-swap vertex table
    // Vtx_t structure (16 bytes):
    //   0x00: ob[3] (3 x s16) - position
    //   0x06: flag (u16)
    //   0x08: tc[2] (2 x s16) - texture coords
    //   0x0C: cn[4] (4 x u8) - color/normal (no swap needed)
    if (IsValidOffset(vertexTable, size)) {
        uint8_t* vtxPtr = data + vertexTable;
        uint8_t* endPtr = data + size;

        // Find the minimum offset among all visited model structures
        // This marks where non-vertex data begins (display lists, model nodes, etc.)
        uint32_t minVisitedOffset = size;
        for (uint32_t off : gVisitedNodes) {
            if (off > vertexTable && off < minVisitedOffset)
                minVisitedOffset = off;
        }
        for (uint32_t off : gVisitedGroups) {
            if (off > vertexTable && off < minVisitedOffset)
                minVisitedOffset = off;
        }
        for (uint32_t off : gVisitedDisplayLists) {
            if (off > vertexTable && off < minVisitedOffset)
                minVisitedOffset = off;
        }
        for (uint32_t off : gVisitedDisplayData) {
            if (off > vertexTable && off < minVisitedOffset)
                minVisitedOffset = off;
        }
        for (uint32_t off : gVisitedProperties) {
            if (off > vertexTable && off < minVisitedOffset)
                minVisitedOffset = off;
        }

        // Also check name tables and header structures
        uint32_t vtxEnd = minVisitedOffset;
        if (root > vertexTable && root < vtxEnd)
            vtxEnd = root;
        if (modelNames > vertexTable && modelNames < vtxEnd)
            vtxEnd = modelNames;
        if (colliderNames > vertexTable && colliderNames < vtxEnd)
            vtxEnd = colliderNames;
        if (zoneNames > vertexTable && zoneNames < vtxEnd)
            vtxEnd = zoneNames;

        size_t vtxSize = vtxEnd - vertexTable;
        size_t numVertices = vtxSize / 16;

        // Store vertex data size for export
        outVtxDataSize = static_cast<uint32_t>(vtxSize);

        for (size_t i = 0; i < numVertices && vtxPtr + 16 <= endPtr; i++) {
            uint16_t* v = reinterpret_cast<uint16_t*>(vtxPtr);
            v[0] = BSWAP16(v[0]); // ob[0]
            v[1] = BSWAP16(v[1]); // ob[1]
            v[2] = BSWAP16(v[2]); // ob[2]
            v[3] = BSWAP16(v[3]); // flag
            v[4] = BSWAP16(v[4]); // tc[0]
            v[5] = BSWAP16(v[5]); // tc[1]
            // cn[4] are bytes, no swap needed
            vtxPtr += 16;
        }
    }

    // Byte-swap name table pointers (arrays of char* terminated by "db" sentinel string)
    // Each table is an array of BE u32 pointers to null-terminated strings.
    // The terminator is an entry whose pointed-to string content is literally "db".
    auto swapNameTable = [&](uint32_t tableOffset) {
        if (!IsValidOffset(tableOffset, size - 4))
            return;

        uint32_t* names = reinterpret_cast<uint32_t*>(data + tableOffset);
        while (reinterpret_cast<uint8_t*>(names) < data + size - 4) {
            uint32_t nameAddr = BSWAP32(*names);
            if (nameAddr == 0) {
                *names = 0;
                break;
            }
            uint32_t nameOffset = N64AddrToOffset(nameAddr);
            // Check if the pointed-to string is "db" (the sentinel terminator)
            if (nameOffset < size - 2) {
                const char* str = reinterpret_cast<const char*>(data + nameOffset);
                if (str[0] == 'd' && str[1] == 'b' && str[2] == '\0') {
                    *names = nameOffset; // still convert, runtime needs the offset
                    break;
                }
            }
            *names = nameOffset;
            names++;
        }
    };

    swapNameTable(modelNames);
    swapNameTable(colliderNames);
    swapNameTable(zoneNames);

    // Clear the collection pointer
    gCollectedDisplayLists = nullptr;
}

std::optional<std::shared_ptr<IParsedData>> PM64ShapeFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    std::vector<PM64DisplayListInfo> collectedDLs;
    uint32_t vtxTableOffset = 0;
    uint32_t vtxDataSize = 0;

    // Check if compressed (YAY0)
    auto compressionType = Decompressor::GetCompressionType(buffer, offset);

    if (compressionType == CompressionType::YAY0) {
        auto decoded = Decompressor::Decode(buffer, offset, CompressionType::YAY0);
        if (!decoded || decoded->size == 0) {
            SPDLOG_ERROR("Failed to decompress YAY0 shape data at offset 0x{:X}", offset);
            return std::nullopt;
        }

        std::vector<uint8_t> shapeData(decoded->data, decoded->data + decoded->size);
        ByteSwapShapeData(shapeData.data(), shapeData.size(), collectedDLs, vtxTableOffset, vtxDataSize);

        return std::make_shared<PM64ShapeData>(std::move(shapeData), std::move(collectedDLs), vtxTableOffset,
                                               vtxDataSize);
    } else {
        // Uncompressed - read raw data with size from YAML
        auto size = GetSafeNode<size_t>(node, "size");
        auto [_, segment] = Decompressor::AutoDecode(node, buffer, size);

        std::vector<uint8_t> shapeData(segment.data, segment.data + segment.size);
        ByteSwapShapeData(shapeData.data(), shapeData.size(), collectedDLs, vtxTableOffset, vtxDataSize);

        return std::make_shared<PM64ShapeData>(std::move(shapeData), std::move(collectedDLs), vtxTableOffset,
                                               vtxDataSize);
    }
}

// Export vertex data as a separate OTR Vertex resource (V1 format with float ob[])
// Returns the resource path used for hashing in G_VTX_OTR_HASH commands
static std::string ExportVertexResource(const std::string& shapeName, const uint8_t* shapeData, uint32_t vtxTableOffset,
                                        uint32_t vtxDataSize) {
    if (vtxDataSize == 0) {
        SPDLOG_WARN("No vertex data to export for shape {}", shapeName);
        return "";
    }

    // Build resource path
    std::string path = shapeName + "/vtx";
    auto writer = LUS::BinaryWriter();

    BaseExporter::WriteHeader(writer, Torch::ResourceType::Vertex, 0);

    // Write vertex count and per-vertex data
    // Shape data has already been byte-swapped to native endian by ByteSwapShapeData
    uint32_t count = vtxDataSize / 16;
    writer.Write(count);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* src = shapeData + vtxTableOffset + i * 16;
        writer.Write(*reinterpret_cast<const int16_t*>(src + 0));  // ob[0]
        writer.Write(*reinterpret_cast<const int16_t*>(src + 2));  // ob[1]
        writer.Write(*reinterpret_cast<const int16_t*>(src + 4));  // ob[2]
        writer.Write(*reinterpret_cast<const uint16_t*>(src + 6)); // flag
        writer.Write(*reinterpret_cast<const int16_t*>(src + 8));  // tc[0]
        writer.Write(*reinterpret_cast<const int16_t*>(src + 10)); // tc[1]
        writer.Write(src[12]);
        writer.Write(src[13]);
        writer.Write(src[14]);
        writer.Write(src[15]); // cn[4]
    }

    // Finish writing and register as companion file
    std::stringstream ss;
    writer.Finish(ss);
    std::string str = ss.str();
    std::vector<char> data(str.begin(), str.end());

    Companion::Instance->RegisterCompanionFile(path, data);

    return path;
}

// Export a single display list as an OTR resource
static void ExportDisplayListResource(const std::string& shapeName, const PM64DisplayListInfo& dlInfo) {
    // Build the resource path
    char pathBuf[256];
    snprintf(pathBuf, sizeof(pathBuf), "%s/dlist_%X", shapeName.c_str(), dlInfo.offset);
    std::string path = pathBuf;

    // Get full OTR path for hash calculation (gCurrentDirectory + path)
    std::string fullPath = Companion::Instance->RelativePath(path);

    auto writer = LUS::BinaryWriter();

    // Write DisplayList resource header
    BaseExporter::WriteHeader(writer, Torch::ResourceType::DisplayList, 0);

    // Write GBI version byte (F3DEX2 for PM64)
    writer.Write(static_cast<int8_t>(GBIVersion::f3dex2));

    // Pad to 8-byte alignment
    while (writer.GetBaseAddress() % 8 != 0)
        writer.Write(static_cast<int8_t>(0xFF));

    // Write G_MARKER with resource hash (using full OTR path)
    uint64_t hash = CRC64(fullPath.c_str());
    writer.Write(static_cast<uint32_t>(G_MARKER << 24));
    writer.Write(static_cast<uint32_t>(0xBEEFBEEF));
    writer.Write(static_cast<uint32_t>(hash >> 32));
    writer.Write(static_cast<uint32_t>(hash & 0xFFFFFFFF));

    // Write commands in OTR format
    // IMPORTANT: libultraship DisplayListFactory expects:
    // - Standard commands: 8 bytes (w0, w1)
    // - OTR-expanded commands: 16 bytes (w0, w1, extra 8 bytes)
    // Expanded opcodes: G_SETTIMG_OTR_HASH, G_DL_OTR_HASH, G_VTX_OTR_HASH,
    //                   G_BRANCH_Z_OTR, G_MARKER, G_MTX_OTR, G_MOVEMEM_OTR
    for (size_t i = 0; i < dlInfo.commands.size(); i += 2) {
        uint32_t w0 = dlInfo.commands[i];
        uint32_t w1 = dlInfo.commands[i + 1];
        uint8_t opcode = (w0 >> 24) & 0xFF;

        if (opcode == F3DEX2_G_SETTIMG) {
            // Replace G_SETTIMG with G_NOOP - PM64 textures are loaded via texture handle system
            // G_NOOP is a standard 8-byte command
            writer.Write(static_cast<uint32_t>(0x00 << 24)); // G_NOOP
            writer.Write(static_cast<uint32_t>(0));
            // NO PADDING - standard command is 8 bytes
        } else if (opcode == F3DEX2_G_VTX) {
            // Emit G_VTX_OTR_HASH - an expanded 16-byte command
            // w0 format: opcode[31:24] | n[19:12] | (v0+n)[7:1]
            // The n and v0+n encoding is preserved from original G_VTX
            char vtxPath[256];
            snprintf(vtxPath, sizeof(vtxPath), "%s/vtx", shapeName.c_str());
            // Use RelativePath to get full OTR path (gCurrentDirectory + vtxPath)
            std::string fullVtxPath = Companion::Instance->RelativePath(vtxPath);
            uint64_t vtxHash = CRC64(fullVtxPath.c_str());

            // Replace opcode with G_VTX_OTR_HASH, keep n and v0 encoding
            uint32_t newW0 = (G_VTX_OTR_HASH << 24) | (w0 & 0x00FFFFFF);
            writer.Write(newW0);
            writer.Write(w1); // w1 is vertex-table-relative offset

            // Write hash (extra 8 bytes for expanded command)
            writer.Write(static_cast<uint32_t>(vtxHash >> 32));
            writer.Write(static_cast<uint32_t>(vtxHash & 0xFFFFFFFF));
        } else if (opcode == F3DEX2_G_DL) {
            // Nested display list - build path for it and write hash
            // G_DL_OTR_HASH is an expanded 16-byte command
            char nestedPath[256];
            snprintf(nestedPath, sizeof(nestedPath), "%s/dlist_%X", shapeName.c_str(), w1);
            // Use RelativePath to get full OTR path (gCurrentDirectory + nestedPath)
            std::string fullNestedPath = Companion::Instance->RelativePath(nestedPath);
            uint64_t nestedHash = CRC64(fullNestedPath.c_str());

            // Write G_DL_OTR_HASH opcode (expanded command - 16 bytes total)
            N64Gfx value = gsSPDisplayListOTRHash(0);
            writer.Write(value.words.w0);
            writer.Write(value.words.w1);
            // Write the hash of the nested display list (extra 8 bytes for expanded command)
            writer.Write(static_cast<uint32_t>(nestedHash >> 32));
            writer.Write(static_cast<uint32_t>(nestedHash & 0xFFFFFFFF));
        } else {
            // Standard command - 8 bytes only
            writer.Write(w0);
            writer.Write(w1);
            // NO PADDING - standard commands are 8 bytes
        }
    }

    // Finish writing and register as companion file
    std::stringstream ss;
    writer.Finish(ss);
    std::string str = ss.str();
    std::vector<char> data(str.begin(), str.end());

    Companion::Instance->RegisterCompanionFile(path, data);
}

ExportResult PM64ShapeBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto shapeData = std::static_pointer_cast<PM64ShapeData>(raw);
    auto writer = LUS::BinaryWriter();

    // Extract shape name from entry name (e.g., "shapes/kmr_02_shape" -> "kmr_02_shape")
    std::string shapeName = entryName;
    size_t lastSlash = entryName.rfind('/');
    if (lastSlash != std::string::npos) {
        shapeName = entryName.substr(lastSlash + 1);
    }

    // Export vertex data as a separate OTR resource
    ExportVertexResource(shapeName, shapeData->mBuffer.data(), shapeData->mVertexTableOffset,
                         shapeData->mVertexDataSize);

    // Export each display list as a separate OTR resource
    for (const auto& dlInfo : shapeData->mDisplayLists) {
        ExportDisplayListResource(shapeName, dlInfo);
    }

    // Write shape blob as before
    WriteHeader(writer, Torch::ResourceType::Blob, 0);
    writer.Write(static_cast<uint32_t>(shapeData->mBuffer.size()));
    writer.Write(reinterpret_cast<char*>(shapeData->mBuffer.data()), shapeData->mBuffer.size());
    writer.Finish(write);

    return std::nullopt;
}

ExportResult PM64ShapeHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern u8 " << symbol << "[];\n";
    return std::nullopt;
}

#ifdef BUILD_UI
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include "imgui.h"
#include "types/RawBuffer.h"
#include "ui/BaseBackend.h"
#include "ui/Widgets.h"

namespace {

uint32_t RD32(const std::vector<uint8_t>& b, uint32_t off) {
    if (off + 4 > b.size()) {
        return 0;
    }
    uint32_t v;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}

uint16_t RD16(const std::vector<uint8_t>& b, uint32_t off) {
    if (off + 2 > b.size()) {
        return 0;
    }
    uint16_t v;
    std::memcpy(&v, b.data() + off, 2);
    return v;
}

std::string RDStr(const std::vector<uint8_t>& b, uint32_t off, size_t maxLen = 64) {
    std::string out;
    while (off < b.size() && b[off] != 0 && out.size() < maxLen) {
        out.push_back((char)b[off++]);
    }
    return out;
}

void MatIdentity(float m[4][4]) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            m[i][j] = i == j ? 1.0f : 0.0f;
        }
    }
}

// Row-vector convention: world = local * parent.
void MatMul(float out[4][4], const float a[4][4], const float b[4][4]) {
    float r[4][4];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    std::memcpy(out, r, sizeof(r));
}

// Fixed-point N64 Mtx: 8 words of interleaved integer parts, 8 of fractions.
void ReadMtx(const std::vector<uint8_t>& blob, uint32_t off, float out[4][4]) {
    MatIdentity(out);
    if (off == 0 || off + 0x40 > blob.size()) {
        return;
    }
    if (Companion::Instance != nullptr && Companion::Instance->GetConfig().gbi.useFloats) {
        std::memcpy(out, blob.data() + off, 0x40);
        return;
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 2; ++j) {
            const int32_t intPart = (int32_t)RD32(blob, off + (i * 2 + j) * 4);
            const uint32_t fracPart = RD32(blob, off + 0x20 + (i * 2 + j) * 4);
            out[i][j * 2] = (float)((int32_t)((intPart & 0xFFFF0000) | (fracPart >> 16))) / 65536.0f;
            out[i][j * 2 + 1] = (float)((int32_t)((intPart << 16) | (fracPart & 0xFFFF))) / 65536.0f;
        }
    }
}

// libultra render mode preset words (Torch's gbi header lacks the G_RM set).
#define PMRM_AA_ZB_OPA_SURF 0x00442078U, 0x00112078U
#define PMRM_ZB_OPA_SURF 0x00442230U, 0x00112230U
#define PMRM_AA_OPA_SURF 0x00442048U, 0x00112048U
#define PMRM_AA_ZB_OPA_DECAL 0x00442D58U, 0x00112D58U
#define PMRM_ZB_OPA_DECAL 0x00442E10U, 0x00112E10U
#define PMRM_AA_ZB_OPA_INTER 0x00442478U, 0x00112478U
#define PMRM_AA_ZB_TEX_EDGE 0x00443078U, 0x00113078U
#define PMRM_AA_TEX_EDGE 0x00443048U, 0x00113048U
#define PMRM_AA_ZB_XLU_SURF 0x004049D8U, 0x001049D8U
#define PMRM_ZB_XLU_SURF 0x00404A50U, 0x00104A50U
#define PMRM_AA_XLU_SURF 0x004041C8U, 0x001041C8U
#define PMRM_AA_ZB_XLU_DECAL 0x00404DD8U, 0x00104DD8U
#define PMRM_ZB_OVL_SURF 0x00404F50U, 0x00104F50U
#define PMRM_AA_ZB_XLU_INTER 0x004045D8U, 0x001045D8U

// pmret model.c ModelRenderModes (RENDER_CLASS_1CYC): exact othermode-L
// cycle words per RenderMode value. The game draws the model tree in order
// with these; there is no cross-model layer sorting.
void RenderModeFor(uint32_t mode, uint32_t& c1, uint32_t& c2) {
    const auto set = [&](uint32_t a, uint32_t b) {
        c1 = a;
        c2 = b;
    };
    switch (mode) {
        case 0x03: set(PMRM_ZB_OPA_SURF); break;      // OPA_NO_AA
        case 0x04: set(PMRM_AA_OPA_SURF); break;      // OPA_NO_ZB
        case 0x05: set(PMRM_AA_ZB_OPA_DECAL); break;  // DECAL_OPA
        case 0x07: set(PMRM_ZB_OPA_DECAL); break;     // DECAL_OPA_NO_AA
        case 0x09: set(PMRM_AA_ZB_OPA_INTER); break;  // INTERSECTING
        case 0x0D:
        case 0x0F: set(PMRM_AA_ZB_TEX_EDGE); break;   // ALPHATEST
        case 0x10: set(PMRM_AA_TEX_EDGE); break;      // ALPHATEST_NO_ZB
        case 0x11:
        case 0x16:
        case 0x22:
        case 0x15:
        case 0x20: set(PMRM_AA_ZB_XLU_SURF); break;   // XLU / SHADOW
        case 0x13: set(PMRM_ZB_XLU_SURF); break;      // XLU_NO_AA
        case 0x14: set(PMRM_AA_XLU_SURF); break;      // XLU_NO_ZB
        case 0x1A:
        case 0x1E: set(PMRM_AA_ZB_XLU_DECAL); break;  // DECAL_XLU
        case 0x1C: set(PMRM_ZB_OVL_SURF); break;      // DECAL_XLU_NO_AA
        case 0x26: set(PMRM_AA_ZB_XLU_INTER); break;  // INTERSECTING_XLU
        default:   set(PMRM_AA_ZB_OPA_SURF); break;   // SURFACE_OPA
    }
}

// --- tex archive ------------------------------------------------------------

struct TexEntry {
    uint32_t rasterOff = 0;
    uint32_t palOff = 0;
    uint16_t w = 0, h = 0;
    uint8_t fmt = 0, siz = 0, cmS = 0, cmT = 0;
    uint8_t extraTiles = 0;  // 1 mipmaps, 2 aux-shared, 3 aux-independent
    uint8_t combineSub = 0;  // main-only combine subtype (0 mod, 1 blend, 2 decal)
    uint32_t auxRasterOff = 0, auxPalOff = 0;
    uint16_t auxW = 0, auxH = 0;
    uint8_t auxFmt = 0, auxSiz = 0, auxCmS = 0, auxCmT = 0;
};

struct TexArchive {
    std::shared_ptr<std::vector<uint8_t>> blob;
    std::map<std::string, TexEntry> entries;
};

uint32_t TexRasterSize(uint16_t w, uint16_t h, uint8_t siz, uint8_t extraTiles) {
    uint32_t size = (uint32_t)w * h;
    if (extraTiles == 1) { // mipmaps
        const int minW[4] = { 16, 8, 4, 2 };
        for (int d = 2; w / d >= minW[siz & 3] && h / d > 0; d *= 2) {
            size += (uint32_t)(w / d) * (h / d);
        }
    }
    switch (siz) {
        case 0: return size / 2;
        case 2: return size * 2;
        case 3: return size * 4;
        default: return size;
    }
}

uint32_t TexPaletteSize(uint8_t fmt, uint8_t siz) {
    if (fmt != 2) { // CI only
        return 0;
    }
    return siz == 1 ? 0x200 : 0x20;
}

// PM64 wrap values map directly onto G_TX flags (0 wrap, 1 mirror, 2 clamp).
std::shared_ptr<TexArchive> IndexTexArchive(const std::shared_ptr<std::vector<uint8_t>>& blob) {
    auto arc = std::make_shared<TexArchive>();
    arc->blob = blob;
    const auto& d = *blob;
    uint32_t off = 0;
    while (off + 0x30 <= d.size()) {
        std::string name = RDStr(d, off, 32);
        if (name.empty()) {
            break;
        }
        const uint16_t auxW = RD16(d, off + 0x20), mainW = RD16(d, off + 0x22);
        const uint16_t auxH = RD16(d, off + 0x24), mainH = RD16(d, off + 0x26);
        const uint8_t extraTiles = d[off + 0x29];
        // Parser rearranged 0x2A to (subType << 6) | combineType.
        const uint8_t combineType = d[off + 0x2A] & 0x3F;
        const uint8_t combineSub = (d[off + 0x2A] >> 6) & 0x3;
        const uint8_t mainFmt = d[off + 0x2B] >> 4, auxFmt = d[off + 0x2B] & 0xF;
        const uint8_t mainSiz = d[off + 0x2C] >> 4, auxSiz = d[off + 0x2C] & 0xF;
        const uint8_t wrapS = d[off + 0x2D] >> 4, auxWrapS = d[off + 0x2D] & 0xF;
        const uint8_t wrapT = d[off + 0x2E] >> 4, auxWrapT = d[off + 0x2E] & 0xF;
        if (mainW == 0 || mainH == 0 || mainW > 1024 || mainH > 1024) {
            break;
        }
        const uint32_t rasterSize = TexRasterSize(mainW, mainH, mainSiz, extraTiles);
        const uint32_t palSize = TexPaletteSize(mainFmt, mainSiz);
        uint32_t auxRaster = 0, auxPal = 0;
        if (extraTiles == 3) {
            auxRaster = TexRasterSize(auxW, auxH, auxSiz, 0);
            auxPal = TexPaletteSize(auxFmt, auxSiz);
        }
        TexEntry e;
        e.rasterOff = off + 0x30;
        e.palOff = e.rasterOff + rasterSize;
        e.w = mainW;
        // AUX_SAME_AS_MAIN packs main+aux vertically in one raster; the game
        // renders tiles of mainH/2.
        e.h = extraTiles == 2 ? (uint16_t)(mainH / 2) : mainH;
        e.fmt = mainFmt;
        e.siz = mainSiz;
        e.cmS = wrapS <= 2 ? wrapS : 0;
        e.cmT = wrapT <= 2 ? wrapT : 0;
        e.extraTiles = extraTiles;
        e.combineSub = combineType < 3 ? combineSub : 0;
        if (extraTiles == 2) {
            // Aux tile = lower half of the main raster, same palette and wraps.
            e.auxRasterOff = e.rasterOff + (uint32_t)mainW * (mainH / 2) * (4u << mainSiz) / 8;
            e.auxPalOff = e.palOff;
            e.auxW = mainW;
            e.auxH = mainH / 2;
            e.auxFmt = mainFmt;
            e.auxSiz = mainSiz;
            e.auxCmS = e.cmS;
            e.auxCmT = e.cmT;
        } else if (extraTiles == 3) {
            e.auxRasterOff = e.palOff + palSize;
            e.auxPalOff = e.auxRasterOff + auxRaster;
            e.auxW = auxW;
            e.auxH = auxH;
            e.auxFmt = auxFmt;
            e.auxSiz = auxSiz;
            e.auxCmS = auxWrapS <= 2 ? auxWrapS : 0;
            e.auxCmT = auxWrapT <= 2 ? auxWrapT : 0;
            if (auxW == 0 || auxH == 0 || auxW > 1024 || auxH > 1024 ||
                (uint64_t)e.auxRasterOff + auxRaster + auxPal > d.size()) {
                e.extraTiles = 0;
                e.auxRasterOff = 0;
            }
        }
        if ((uint64_t)e.rasterOff + rasterSize + palSize <= d.size()) {
            arc->entries[name] = e;
        }
        off += 0x30 + rasterSize + palSize + auxRaster + auxPal;
    }
    return arc;
}

// Finds the tex archive for a shape: same area prefix (e.g. arn_02_shape -> arn_tex).
std::shared_ptr<TexArchive> TexArchiveForShape(const std::string& shapeName) {
    static std::map<std::string, std::shared_ptr<TexArchive>> sCache;
    const auto slash = shapeName.find_last_of('/');
    const std::string base = slash != std::string::npos ? shapeName.substr(slash + 1) : shapeName;
    const std::string area = base.substr(0, base.find('_'));
    if (area.empty()) {
        return nullptr;
    }
    const auto cit = sCache.find(area);
    if (cit != sCache.end()) {
        return cit->second;
    }
    std::shared_ptr<TexArchive> found;
    for (const auto& [file, results] : Companion::Instance->GetParseResults()) {
        for (const auto& r : results) {
            if (r.type != "PM64:MAP_TEXTURE" || !r.data.has_value()) {
                continue;
            }
            const auto rslash = r.name.find_last_of('/');
            const std::string rbase = rslash != std::string::npos ? r.name.substr(rslash + 1) : r.name;
            if (rbase.rfind(area + "_tex", 0) == 0 || rbase == area) {
                auto buf = std::static_pointer_cast<RawBuffer>(r.data.value());
                found = IndexTexArchive(std::make_shared<std::vector<uint8_t>>(buf->mBuffer));
                break;
            }
        }
        if (found != nullptr) {
            break;
        }
    }
    if (found != nullptr && std::getenv("TORCH_PM64_DEBUG") != nullptr) {
        printf("[shape] tex archive '%s': %zu entries\n", area.c_str(), found->entries.size());
        for (const auto& [n, e] : found->entries) {
            printf("[shape]   %-24s %ux%u fmt=%u siz=%u wrap=(%u,%u) raster=0x%X pal=0x%X extra=%u sub=%u aux=%ux%u fmt=%u siz=%u\n",
                   n.c_str(), e.w, e.h, e.fmt, e.siz, e.cmS, e.cmT, e.rasterOff, e.palOff, e.extraTiles, e.combineSub,
                   e.auxW, e.auxH, e.auxFmt, e.auxSiz);
        }
    }
    sCache[area] = found;
    return found;
}

// --- shape flatten ----------------------------------------------------------

struct ShapeModel {
    std::vector<UI::ModelPart> parts;
    size_t nodeCount = 0;
    size_t texturedParts = 0;
};

struct WalkCtx {
    const std::vector<uint8_t>* blob;
    std::shared_ptr<std::vector<uint8_t>> blobRef;
    UI::GfxBundle* bundle;
    std::shared_ptr<TexArchive> tex;
    std::string itemName;
    ShapeModel* out;
    int depth = 0;
};

void WalkNode(WalkCtx& ctx, uint32_t nodeOff, const float parent[4][4]) {
    const auto& blob = *ctx.blob;
    if (nodeOff == 0 || nodeOff + 0x14 > blob.size() || ctx.depth > 64 || ctx.out->parts.size() > 4096) {
        return;
    }
    ctx.out->nodeCount++;
    const uint32_t nodeType = RD32(blob, nodeOff + 0x00);
    const uint32_t displayData = RD32(blob, nodeOff + 0x04);
    const int32_t numProps = (int32_t)RD32(blob, nodeOff + 0x08);
    const uint32_t propList = RD32(blob, nodeOff + 0x0C);
    const uint32_t groupData = RD32(blob, nodeOff + 0x10);

    std::string texName;
    uint32_t renderMode = 1;
    uint32_t groupType = 0;
    if (numProps > 0 && numProps < 128 && propList != 0) {
        for (int32_t i = 0; i < numProps; ++i) {
            const uint32_t p = propList + (uint32_t)i * 0xC;
            const uint32_t key = RD32(blob, p);
            const uint32_t val = RD32(blob, p + 8);
            if (key == 0x5E) { // MODEL_PROP_KEY_TEXTURE_NAME
                texName = RDStr(blob, val);
            } else if (key == 0x5C) { // MODEL_PROP_KEY_RENDER_MODE
                renderMode = val;
            } else if (key == 0x60) { // MODEL_PROP_KEY_GROUP_INFO
                groupType = val;
            }
        }
    }

    // Only MODEL leaves (type 2) draw. Group rendering expands children into
    // per-child models (func_80117D00); group/root display lists re-call every
    // child list and drawing them double-draws the map untextured.
    constexpr uint32_t kShapeTypeModel = 2;
    const bool drawSelf = nodeType == kShapeTypeModel;
    (void)groupType;

    float world[4][4];
    std::memcpy(world, parent, sizeof(world));
    uint32_t numChildren = 0, childList = 0;
    if (groupData != 0 && groupData + 0x14 <= blob.size()) {
        const uint32_t mtxOff = RD32(blob, groupData);
        numChildren = RD32(blob, groupData + 0x0C);
        childList = RD32(blob, groupData + 0x10);
        if (mtxOff != 0) {
            float local[4][4];
            ReadMtx(blob, mtxOff, local);
            MatMul(world, local, parent);
        }
    }

    if (drawSelf && displayData != 0 && displayData + 8 <= blob.size()) {
        const uint32_t dlOff = RD32(blob, displayData);
        if (dlOff != 0 && dlOff < blob.size()) {
            char suffix[16];
            snprintf(suffix, sizeof(suffix), "#%X", dlOff);
            UI::ModelPart part;
            part.resource = ctx.itemName + suffix;
            const bool registered = UI::GetBackend() != nullptr
                                        ? UI::GetBackend()->RegisterGameDList(part.resource, *ctx.bundle, dlOff)
                                        : true;
            if (registered) {
                std::memcpy(part.mtx, world, sizeof(part.mtx));
                // Tree order is the game's draw order; keep one layer so the
                // preview's stable sort preserves it.
                part.layer = 1;
                RenderModeFor(renderMode, part.renderMode1, part.renderMode2);
                part.unlit = true; // shape geometry is vertex-colored
                if (std::getenv("TORCH_PM64_DEBUG") != nullptr) {
                    const bool found = !texName.empty() && ctx.tex != nullptr &&
                                       ctx.tex->entries.count(texName) != 0;
                    printf("[shape] %s dl=0x%X rmode=%u tex='%s' %s\n", ctx.itemName.c_str(), dlOff, renderMode,
                           texName.c_str(),
                           texName.empty()          ? "(untextured)"
                           : ctx.tex == nullptr     ? "NO ARCHIVE"
                           : found                  ? "ok"
                                                    : "NOT FOUND");
                }
                if (!texName.empty() && ctx.tex != nullptr) {
                    const auto tit = ctx.tex->entries.find(texName);
                    if (tit != ctx.tex->entries.end()) {
                        const TexEntry& te = tit->second;
                        auto texture = std::make_shared<UI::PartTexture>();
                        texture->blob = ctx.tex->blob;
                        texture->rasterOffset = te.rasterOff;
                        texture->paletteOffset = te.palOff;
                        texture->width = te.w;
                        texture->height = te.h;
                        texture->fmt = te.fmt;
                        texture->siz = te.siz;
                        texture->cmS = te.cmS;
                        texture->cmT = te.cmT;
                        texture->combine = te.combineSub;
                        if (te.extraTiles == 2 || te.extraTiles == 3) {
                            texture->auxMode = te.extraTiles;
                            texture->auxRasterOffset = te.auxRasterOff;
                            texture->auxPaletteOffset = te.auxPalOff;
                            texture->auxWidth = te.auxW;
                            texture->auxHeight = te.auxH;
                            texture->auxFmt = te.auxFmt;
                            texture->auxSiz = te.auxSiz;
                            texture->auxCmS = te.auxCmS;
                            texture->auxCmT = te.auxCmT;
                            // Aux blends are two-cycle (RENDER_CLASS_2CYC):
                            // pass in blender cycle 1, surface mode in cycle 2.
                            part.renderMode1 = 0x0C084000; // G_RM_PASS
                            part.cycleType = 2;
                        }
                        part.texture = std::move(texture);
                        ctx.out->texturedParts++;
                    }
                }
                ctx.out->parts.push_back(std::move(part));
            }
        }
    }

    if (numChildren > 0 && numChildren < 1024 && childList != 0) {
        ctx.depth++;
        for (uint32_t i = 0; i < numChildren; ++i) {
            WalkNode(ctx, RD32(blob, childList + i * 4), world);
        }
        ctx.depth--;
    }
}

// --- backgrounds --------------------------------------------------------------

std::string BaseName(const std::string& name) {
    const auto slash = name.find_last_of('/');
    return slash != std::string::npos ? name.substr(slash + 1) : name;
}

// All parsed PM64:BACKGROUND asset names.
const std::vector<std::string>& BackgroundNames() {
    static std::vector<std::string> names;
    static bool init = false;
    if (!init) {
        init = true;
        for (const auto& [file, results] : Companion::Instance->GetParseResults()) {
            for (const auto& r : results) {
                if (r.type == "PM64:BACKGROUND" && r.data.has_value()) {
                    names.push_back(r.name);
                }
            }
        }
        std::sort(names.begin(), names.end());
    }
    return names;
}

struct BgImage {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
};

// Decodes a background (CI8 raster @0x210, big-endian RGBA5551 palette @0x10)
// to RGBA8; cached per asset.
const BgImage* GetBackground(const std::string& name) {
    static std::map<std::string, BgImage> sCache;
    auto it = sCache.find(name);
    if (it == sCache.end()) {
        BgImage img;
        std::shared_ptr<RawBuffer> buf;
        for (const auto& [file, results] : Companion::Instance->GetParseResults()) {
            for (const auto& r : results) {
                if (r.type == "PM64:BACKGROUND" && r.name == name && r.data.has_value()) {
                    buf = std::static_pointer_cast<RawBuffer>(r.data.value());
                    break;
                }
            }
            if (buf != nullptr) {
                break;
            }
        }
        if (buf != nullptr && buf->mBuffer.size() >= 0x210) {
            const auto& d = buf->mBuffer;
            const uint32_t rasterOff = RD32(d, 0x00);
            const uint32_t palOff = RD32(d, 0x04);
            const int w = RD16(d, 0x0C);
            const int h = RD16(d, 0x0E);
            if (w > 0 && h > 0 && w <= 1024 && h <= 1024 && (uint64_t)palOff + 0x200 <= d.size() &&
                (uint64_t)rasterOff + (uint64_t)w * h <= d.size()) {
                img.rgba.resize((size_t)w * h * 4);
                for (int i = 0; i < w * h; ++i) {
                    const uint8_t ci = d[rasterOff + i];
                    const uint16_t c = ((uint16_t)d[palOff + ci * 2] << 8) | d[palOff + ci * 2 + 1];
                    img.rgba[i * 4 + 0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
                    img.rgba[i * 4 + 1] = (uint8_t)(((c >> 6) & 31) * 255 / 31);
                    img.rgba[i * 4 + 2] = (uint8_t)(((c >> 1) & 31) * 255 / 31);
                    img.rgba[i * 4 + 3] = 255;
                }
                img.w = w;
                img.h = h;
            }
        }
        if (std::getenv("TORCH_PM64_DEBUG") != nullptr) {
            printf("[bg] %s: buf=%zu %dx%d\n", name.c_str(), buf != nullptr ? buf->mBuffer.size() : 0, img.w, img.h);
        }
        it = sCache.emplace(name, img).first;
    }
    return !it->second.rgba.empty() ? &it->second : nullptr;
}

// Backgrounds are matched per map area; a few areas use another area's sky.
std::string DefaultBackgroundFor(const std::string& shapeName) {
    std::string area = BaseName(shapeName);
    area = area.substr(0, area.find('_'));
    static const std::map<std::string, std::string> kAlias = {
        { "dro", "sbk" }, { "isk", "sbk" }, { "flo", "fla" }, { "mim", "obk" }, { "tik", "jan" },
    };
    const auto ait = kAlias.find(area);
    if (ait != kAlias.end()) {
        area = ait->second;
    }
    std::string found;
    for (const auto& n : BackgroundNames()) {
        if (BaseName(n) == area + "_bg") {
            found = n;
            break;
        }
    }
    if (std::getenv("TORCH_PM64_DEBUG") != nullptr) {
        fprintf(stderr, "[bg] default for %s: area=%s -> '%s' (%zu candidates)\n", shapeName.c_str(), area.c_str(),
                found.c_str(), BackgroundNames().size());
    }
    return found;
}

std::map<std::string, ShapeModel> sShapeCache;
std::map<std::string, UI::OrbitView> sShapeViews;

const ShapeModel& BuildShapeModel(const ParseResultData& item) {
    auto it = sShapeCache.find(item.name);
    if (it != sShapeCache.end()) {
        return it->second;
    }
    ShapeModel model;
    auto data = std::static_pointer_cast<PM64ShapeData>(item.data.value());
    auto blob = std::make_shared<std::vector<uint8_t>>(data->mBuffer);

    UI::GfxBundle bundle;
    bundle.blob = blob;
    bundle.vtxBase = data->mVertexTableOffset;
    bundle.vtxSize = data->mVertexDataSize;
    const bool useFloats = Companion::Instance != nullptr && Companion::Instance->GetConfig().gbi.useFloats;
    for (const auto& dl : data->mDisplayLists) {
        UI::GfxBundleDList out;
        out.offset = dl.offset;
        out.words = dl.commands;
        if (useFloats) {
            // Parser rescaled G_VTX offsets for 24-byte float vertices; the
            // blob still holds 16-byte ones.
            for (size_t i = 0; i + 1 < out.words.size(); i += 2) {
                if ((out.words[i] >> 24) == 0x01) {
                    out.words[i + 1] = out.words[i + 1] / 24 * 16;
                }
            }
        }
        bundle.dlists.push_back(std::move(out));
    }

    WalkCtx ctx;
    ctx.blob = blob.get();
    ctx.blobRef = blob;
    ctx.bundle = &bundle;
    ctx.tex = TexArchiveForShape(item.name);
    ctx.itemName = item.name;
    ctx.out = &model;

    float identity[4][4];
    MatIdentity(identity);
    WalkNode(ctx, RD32(*blob, 0), identity);
    if (const char* cap = std::getenv("TORCH_PM64_MAXPARTS")) {
        const size_t n = (size_t)atoi(cap);
        if (n > 0 && model.parts.size() > n) {
            model.parts.resize(n);
        }
    }
    return sShapeCache.emplace(item.name, std::move(model)).first->second;
}

} // namespace

void PM64ShapeDebugBuild(const ParseResultData& item) {
    if (!item.data.has_value()) {
        return;
    }
    const ShapeModel& model = BuildShapeModel(item);
    printf("[shape] %s: %zu nodes, %zu parts, %zu textured\n", item.name.c_str(), model.nodeCount,
           model.parts.size(), model.texturedParts);
}

float PM64ShapeFactoryUI::GetItemHeight(const ParseResultData& item) {
    return 86.0f + UI::PreviewBlockHeight(item.name);
}

void PM64ShapeFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const ShapeModel& model = BuildShapeModel(item);
    ImGui::TextDisabled("map shape  \xe2\x80\x94  %zu nodes, %zu parts (%zu textured)", model.nodeCount,
                        model.parts.size(), model.texturedParts);
    if (model.parts.empty()) {
        ImGui::TextDisabled("nothing drawable");
        return;
    }

    static std::map<std::string, std::string> sBgChoice;
    auto bgIt = sBgChoice.find(item.name);
    if (bgIt == sBgChoice.end()) {
        bgIt = sBgChoice.emplace(item.name, DefaultBackgroundFor(item.name)).first;
    }
    std::string& bgChoice = bgIt->second;
    ImGui::SetNextItemWidth(170.0f);
    const std::string bgLabel = "bg: " + (bgChoice.empty() ? "none" : BaseName(bgChoice));
    if (ImGui::BeginCombo("##shapebg", bgLabel.c_str())) {
        if (ImGui::Selectable("none", bgChoice.empty())) {
            bgChoice.clear();
        }
        for (const auto& n : BackgroundNames()) {
            if (ImGui::Selectable(BaseName(n).c_str(), n == bgChoice)) {
                bgChoice = n;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    UI::LightingControls();

    UI::OrbitView& view = sShapeViews[item.name];
    if (std::getenv("TORCH_UI_SPIN") != nullptr) {
        static float sSpinT = 0.0f;
        sSpinT += 0.008f;
        view.yaw += 0.02f;
        view.zoom = 1.0f + 0.75f * std::sin(sSpinT);
        if (view.zoom < 0.3f) {
            view.zoom = 0.3f;
        }
    }
    // Apply the backdrop when the selection changes (the backend re-renders).
    static std::map<std::string, std::string> sBgApplied;
    if (sBgApplied[item.name] != bgChoice) {
        sBgApplied[item.name] = bgChoice;
        if (std::getenv("TORCH_PM64_DEBUG") != nullptr) {
            fprintf(stderr, "[bg] apply %s -> '%s'\n", item.name.c_str(), bgChoice.c_str());
        }
        const BgImage* bg = bgChoice.empty() ? nullptr : GetBackground(bgChoice);
        if (bg != nullptr) {
            UI::GetBackend()->SetPreviewBackdrop(item.name, bg->rgba.data(), bg->w, bg->h);
        } else {
            UI::GetBackend()->SetPreviewBackdrop(item.name, nullptr, 0, 0);
        }
    }

    const UI::PreviewCanvas canvas = UI::BeginResizableCanvas("##shapeview", item.name, view);
    if (canvas.visible) {
        UI::GetBackend()->DrawModelParts(item.name, model.parts, canvas.origin, canvas.size, view);
    }
}
#endif // BUILD_UI
