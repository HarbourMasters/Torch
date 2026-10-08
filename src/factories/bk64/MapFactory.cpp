#include "MapFactory.h"

#include "BKByteUtils.h"
#include "BKEmitText.h"
#include "BKResource.h"
#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace BK64 {

static const std::unordered_map<uint8_t, const char*> sNodeCategories = {
    { 3, "Warp" },       { 4, "ContactTrigger" },    { 6, "Actor" }, { 7, "EnemyArea" }, { 8, "PathPoint" },
    { 9, "CameraZone" }, { 0xA, "CollectibleZone" },
};
static constexpr uint8_t CATEGORY_CAMERA_ZONE = 9;
static constexpr uint32_t MODEL_ASSET_OFFSET = 0x2D1;
static constexpr uint32_t SPRITE_ASSET_OFFSET = 0x572;

enum PropKind { PROP_SPRITE, PROP_ACTOR_SLOT, PROP_MODEL };
static const char* const sPropKinds[] = { "Sprite", "ActorSlot", "Model" };
static const char* const sCameraTypes[] = { "Empty", "Pivot", "Static", "Dolly", "ZoomPreset" };
// func_803422D4 in spline_pathfollow.c.
static const char* const sEventKinds[] = { nullptr,     "MusicChannels", "PlayTrack", "PlaySound",
                                           "StopTrack", "StopSound",     "MapFlag",   "Cutscene" };
static const char* const sApplyNames[] = { "Pause", "Speed", "Animation" };

// Chunk parsers, defined further down
static void ParseCubeSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map, size_t totalSize,
                             const std::string& symbol);
static void ParseCameraSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map, const std::string& symbol);
static void ParseLightingSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map, const std::string& symbol);

// A node is five big-endian words in the ROM, and so is a waypoint, which reads them as Struct_glspline_t0 or _t1.
static void NodeToWords(const NodeProp& node, uint32_t words[5]) {
    words[0] = (uint32_t)(uint16_t)node.position[0] << 16 | (uint16_t)node.position[1];
    words[1] = (uint32_t)(uint16_t)node.position[2] << 16 | node.radius << 7 | node.bit6 << 1 | node.bit0;
    words[2] = (uint32_t)node.unk8 << 16 | node.unkA << 8 | node.padB;
    words[3] = (uint32_t)node.yaw << 23 | node.scale;
    words[4] = (uint32_t)node.unk10_31 << 20 | node.unk10_19 << 8 | node.pad10_7 << 7 | node.unk10_6 << 6 |
               node.pad10_5 << 2 | node.unk10_0;
}

static void PropToWords(const Prop& prop, uint32_t words[3]) {
    for (int i = 0; i < 3; i++) {
        words[i] = ReadU32BE(prop.raw + i * 4);
    }
}

static PropKind KindOf(const Prop& prop) {
    return (prop.raw[11] & 1) ? PROP_ACTOR_SLOT : (prop.raw[11] & 2) ? PROP_MODEL : PROP_SPRITE;
}

static int16_t PropPosition(const Prop& prop, int axis) {
    return ReadS16BE(prop.raw + 4 + axis * 2);
}

static uint16_t PropHalf(const Prop& prop, int at) {
    return ReadU16BE(prop.raw + at);
}

// cube_positionToIndices in gccube.c, f32 maths included.
static int32_t CellOf(int16_t position, int32_t min, int32_t max) {
    const float scaled = position / 1000.0f;
    const int32_t cell = position >= 0 ? (int32_t)scaled : (int32_t)(scaled + -1.0f);
    return std::clamp(cell, min, max);
}

static const char* NodeCategoryName(uint8_t category) {
    const auto it = sNodeCategories.find(category);
    return it != sNodeCategories.end() ? it->second : "";
}

ExportResult MapHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                       YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto map = std::static_pointer_cast<MapData>(raw);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    // Each cube's NodeProp/Prop arrays need an extern decl before the Cube array references them
    for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
        const auto& cube = map->mCubes[cubeIdx];

        if (!cube.nodeProps.empty()) {
            write << "extern NodeProp " << symbol << "_Cube" << cubeIdx << "_NodeProps[" << cube.nodeProps.size()
                  << "];\n";
        }

        if (!cube.props.empty()) {
            write << "extern Prop " << symbol << "_Cube" << cubeIdx << "_Props[" << cube.props.size() << "];\n";
        }
    }

    write << "extern Cube " << symbol << "[" << map->mCubes.size() << "];\n";
    return std::nullopt;
}

ExportResult MapCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                     YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto map = std::static_pointer_cast<MapData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    // OTR mode: just the cube array, prop pointers stay NULL (resolved from the OTR at load)
    if (Companion::Instance->IsOTRMode()) {
        write << "Cube " << symbol << "[] = {\n";
        for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
            const auto& cube = map->mCubes[cubeIdx];
            write << fourSpaceTab << "{\n";
            write << fourSpaceTab << fourSpaceTab << "/* coord */ " << cube.x << ", " << cube.y << ", " << cube.z
                  << ",\n";
            write << fourSpaceTab << fourSpaceTab << "/* prop1Cnt */ " << cube.nodeProps.size() << ", /* prop2Cnt */ "
                  << cube.props.size() << ",\n";
            write << fourSpaceTab << fourSpaceTab << "/* unk0_4 */ " << cube.nodeProps.size() << ",\n";

            write << fourSpaceTab << fourSpaceTab << "/* prop1Ptr */ NULL,\n";
            write << fourSpaceTab << fourSpaceTab << "/* prop2Ptr */ NULL\n";

            write << fourSpaceTab << "},\n";
        }
        write << "};\n\n";
        return offset;
    }

    // Export NodeProps for each cube
    for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
        const auto& cube = map->mCubes[cubeIdx];

        if (!cube.nodeProps.empty()) {
            write << "NodeProp " << symbol << "_Cube" << cubeIdx << "_NodeProps[] = {\n";
            for (const auto& nodeProp : cube.nodeProps) {
                write << fourSpaceTab << "{\n";
                write << fourSpaceTab << fourSpaceTab << "/* pos */ " << nodeProp.position[0] << ", "
                      << nodeProp.position[1] << ", " << nodeProp.position[2] << ",\n";
                write << fourSpaceTab << fourSpaceTab << "/* radius */ " << nodeProp.radius << ",\n";
                write << fourSpaceTab << fourSpaceTab << "/* type */ " << (uint32_t)nodeProp.bit6;
                if (nodeProp.bit0) {
                    write << ", /* waypoint */ ";
                } else if (const char* category = NodeCategoryName(nodeProp.bit6); *category) {
                    write << ", /* " << category << " */ ";
                } else {
                    write << ", ";
                }
                write << std::hex << "0x" << nodeProp.unk8 << std::dec << ",\n";
                write << fourSpaceTab << fourSpaceTab << "/* yaw */ " << nodeProp.yaw << ", /* scale */ "
                      << nodeProp.scale << "\n";
                write << fourSpaceTab << "},\n";
            }
            write << "};\n\n";
        }

        if (!cube.props.empty()) {
            write << "Prop " << symbol << "_Cube" << cubeIdx << "_Props[] = {\n";
            for (const auto& prop : cube.props) {
                const PropKind kind = KindOf(prop);
                uint32_t words[3];
                PropToWords(prop, words);
                const auto position = "{ " + std::to_string(PropPosition(prop, 0)) + ", " +
                                      std::to_string(PropPosition(prop, 1)) + ", " +
                                      std::to_string(PropPosition(prop, 2)) + " }, ";

                write << fourSpaceTab << "{ ." << sPropKinds[kind] << " = { ";
                if (kind == PROP_ACTOR_SLOT) {
                    // The marker is a pointer the game fills in
                    write << "NULL, " << position << std::hex << "0x" << PropHalf(prop, 10) << std::dec;
                } else if (kind == PROP_MODEL) {
                    write << std::hex << "0x" << PropHalf(prop, 0) << std::dec << ", " << (int)prop.raw[2] << ", "
                          << (int)prop.raw[3] << ", " << position << (int)prop.raw[10] << ", " << std::hex << "0x"
                          << (int)prop.raw[11] << std::dec;
                } else {
                    write << std::hex << "0x" << words[0] << std::dec << ", " << position << std::hex << "0x"
                          << PropHalf(prop, 10) << std::dec;
                }

                write << " } },\n";
            }
            write << "};\n\n";
        }
    }

    // Export cube array
    write << "Cube " << symbol << "[] = {\n";
    for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
        const auto& cube = map->mCubes[cubeIdx];
        write << fourSpaceTab << "{\n";
        write << fourSpaceTab << fourSpaceTab << "/* coord */ " << cube.x << ", " << cube.y << ", " << cube.z << ",\n";
        write << fourSpaceTab << fourSpaceTab << "/* prop1Cnt */ " << cube.nodeProps.size() << ", /* prop2Cnt */ "
              << cube.props.size() << ",\n";
        write << fourSpaceTab << fourSpaceTab << "/* unk0_4 */ " << cube.nodeProps.size() << ",\n";

        if (!cube.nodeProps.empty()) {
            write << fourSpaceTab << fourSpaceTab << "/* prop1Ptr */ " << symbol << "_Cube" << cubeIdx
                  << "_NodeProps,\n";
        } else {
            write << fourSpaceTab << fourSpaceTab << "/* prop1Ptr */ NULL,\n";
        }

        if (!cube.props.empty()) {
            write << fourSpaceTab << fourSpaceTab << "/* prop2Ptr */ " << symbol << "_Cube" << cubeIdx << "_Props\n";
        } else {
            write << fourSpaceTab << fourSpaceTab << "/* prop2Ptr */ NULL\n";
        }

        write << fourSpaceTab << "},\n";
    }
    write << "};\n\n";

    return offset;
}

ExportResult MapBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                       YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto map = std::static_pointer_cast<MapData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKMap, 0);

    // --- Cube section ---
    writer.Write((uint32_t)map->mCubes.size());
    writer.Write(map->mCubeMin[0]);
    writer.Write(map->mCubeMin[1]);
    writer.Write(map->mCubeMin[2]);
    writer.Write(map->mCubeMax[0]);
    writer.Write(map->mCubeMax[1]);
    writer.Write(map->mCubeMax[2]);

    for (const auto& cube : map->mCubes) {
        const uint32_t nodeCount = (uint32_t)cube.nodeProps.size();
        uint32_t cubeHeader = ((cube.x & 0x1F) << 27) | ((cube.y & 0x1F) << 22) | ((cube.z & 0x1F) << 17) |
                              ((nodeCount & 0x3F) << 11) | ((cube.props.size() & 0x3F) << 5) |
                              ((nodeCount & 0x1F) << 0);
        writer.Write(cubeHeader);

        // NodeProps inline
        writer.Write((uint32_t)cube.nodeProps.size());
        for (const auto& np : cube.nodeProps) {
            writer.Write(np.position[0]);
            writer.Write(np.position[1]);
            writer.Write(np.position[2]);
            writer.Write(static_cast<uint16_t>(np.radius));
            writer.Write(static_cast<uint8_t>(np.bit6));
            writer.Write(static_cast<uint8_t>(np.bit0));
            writer.Write(np.unk8);
            writer.Write(np.unkA);
            writer.Write(np.padB);
            writer.Write(static_cast<uint16_t>(np.yaw));
            writer.Write(np.scale);
            writer.Write(static_cast<uint16_t>(np.unk10_31));
            writer.Write(static_cast<uint16_t>(np.unk10_19));
            writer.Write(static_cast<uint8_t>(np.pad10_7));
            writer.Write(static_cast<uint8_t>(np.unk10_6));
            writer.Write(static_cast<uint8_t>(np.pad10_5));
            writer.Write(static_cast<uint8_t>(np.unk10_0));
        }

        // Props inline (raw 12-byte structs)
        writer.Write((uint32_t)cube.props.size());
        for (const auto& prop : cube.props) {
            writer.Write((char*)prop.raw, 12);
        }
    }

    // --- Camera section ---
    writer.Write((uint32_t)map->mCameraNodes.size());
    for (const auto& cam : map->mCameraNodes) {
        writer.Write(cam.index);
        writer.Write(cam.type);
        switch (cam.type) {
            case 1:
                writer.Write(cam.data.type1.position[0]);
                writer.Write(cam.data.type1.position[1]);
                writer.Write(cam.data.type1.position[2]);
                writer.Write(cam.data.type1.horizontalSpeed);
                writer.Write(cam.data.type1.verticalSpeed);
                writer.Write(cam.data.type1.rotation);
                writer.Write(cam.data.type1.accelaration);
                writer.Write(cam.data.type1.pitchYawRoll[0]);
                writer.Write(cam.data.type1.pitchYawRoll[1]);
                writer.Write(cam.data.type1.pitchYawRoll[2]);
                writer.Write(cam.data.type1.unknownFlag);
                break;
            case 2:
                writer.Write(cam.data.type2.position[0]);
                writer.Write(cam.data.type2.position[1]);
                writer.Write(cam.data.type2.position[2]);
                writer.Write(cam.data.type2.pitchYawRoll[0]);
                writer.Write(cam.data.type2.pitchYawRoll[1]);
                writer.Write(cam.data.type2.pitchYawRoll[2]);
                break;
            case 3:
                writer.Write(cam.data.type3.position[0]);
                writer.Write(cam.data.type3.position[1]);
                writer.Write(cam.data.type3.position[2]);
                writer.Write(cam.data.type3.horizontalSpeed);
                writer.Write(cam.data.type3.verticalSpeed);
                writer.Write(cam.data.type3.rotation);
                writer.Write(cam.data.type3.accelaration);
                writer.Write(cam.data.type3.closeDistance);
                writer.Write(cam.data.type3.farDistance);
                writer.Write(cam.data.type3.pitchYawRoll[0]);
                writer.Write(cam.data.type3.pitchYawRoll[1]);
                writer.Write(cam.data.type3.pitchYawRoll[2]);
                writer.Write(cam.data.type3.unknownFlag);
                break;
            case 4:
                writer.Write(cam.data.type4.unknownFlag);
                break;
            case 0:
                // Type 0 is legitimately empty
                break;
            default:
                SPDLOG_WARN("[BK64:MAP] Binary export: unknown camera type {}", cam.type);
                break;
        }
    }

    // --- Lighting section ---
    writer.Write((uint32_t)map->mLightingVectors.size());
    for (const auto& light : map->mLightingVectors) {
        writer.Write(light.position[0]);
        writer.Write(light.position[1]);
        writer.Write(light.position[2]);
        writer.Write(light.fadeRadii[0]);
        writer.Write(light.fadeRadii[1]);
        writer.Write(light.rgb[0]);
        writer.Write(light.rgb[1]);
        writer.Write(light.rgb[2]);
    }

    writer.Finish(write);
    return OffsetEntry{ 0 };
}

ExportResult MapModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                        YAML::Node& node, std::string* replacement) {
    const auto map = std::static_pointer_cast<MapData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";
    write << MapToYaml(*map, symbol);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> MapFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto symbol = GetSafeNode<std::string>(node, "symbol");

    try {
        auto [_, segment] = Decompressor::AutoDecode(node, buffer);
        if (!segment.data || segment.size == 0) {
            SPDLOG_ERROR("Decompression returned null for symbol: {}", symbol);
            return std::nullopt;
        }
        return ReadMapRom(std::vector<uint8_t>(segment.data, segment.data + segment.size), symbol);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("MapFactory parse error for {}: {}", symbol, e.what());
        return std::nullopt;
    }
}

std::shared_ptr<MapData> ReadMapRom(const std::vector<uint8_t>& bytes, const std::string& symbol) {
    // BK64 map format, post-decompression. A run of chunks, each led by a
    // type marker (see gsworld_load):
    //   Type 0x00: End of file
    //   Type 0x01: Cube data section (grid bounds + cube definitions)
    //   Type 0x02: Reserved/empty
    //   Type 0x03: Camera node section
    //   Type 0x04: Lighting vector section
    //
    // Cube Section Format (type 0x01):
    //   - Min cube position (s32[3])
    //   - Max cube position (s32[3])
    //   - For each cube in grid: CubeHeader + NodeProps + Props
    //   - NodeProp: 20 bytes each
    //   - Prop: 12 bytes each

    LUS::BinaryReader reader((char*)bytes.data(), bytes.size());
    reader.SetEndianness(Torch::Endianness::Big);

    auto map = std::make_shared<MapData>();

    // Walk chunks until the 0x00 end marker (or we run out of data)
    while (reader.GetBaseAddress() < bytes.size()) {
        uint8_t chunkType = reader.ReadUByte();

        if (chunkType == 0x00) {
            // End of file
            break;
        } else if (chunkType == 0x01) {
            ParseCubeSection(reader, map, bytes.size(), symbol);
        } else if (chunkType == 0x02) {
            // Reserved/empty in the decomp; nothing to read
            continue;
        } else if (chunkType == 0x03) {
            ParseCameraSection(reader, map, symbol);
        } else if (chunkType == 0x04) {
            ParseLightingSection(reader, map, symbol);
        } else {
            SPDLOG_WARN("[BK64:MAP] Unknown chunk type 0x{:02X} at offset 0x{:X} "
                        "in asset {}",
                        chunkType, reader.GetBaseAddress() - 1, symbol);
            break;
        }
    }

    return map;
}

// Reads the inner contents of one cube (this is code7AF80_initCubeFromFile).
// Reached when the per-cube wrapper below hits marker 0x03.
static void ReadCubeContent(LUS::BinaryReader& reader, CubeData& cube, const std::string& symbol) {
    // Layout from the decomp (code7AF80_initCubeFromFile in
    // actor_cubepropsystem.c):
    //   Optional NodeProps (new):  0x0A + count (u8) + 0x0B + NodeProp data (20
    //   bytes each) Optional NodeProps (old):  0x06 + count (u8) + 0x07 +
    //   OtherNode data (12 bytes each) Optional Props:            0x08 + count
    //   (u8) + 0x09 + Prop data (12 bytes each)
    // All optional — a cube may carry nothing at all.

    // NodeProps
    size_t peekPos = reader.GetBaseAddress();
    uint8_t marker = reader.ReadUByte();
    reader.Seek(peekPos, LUS::SeekOffsetType::Start);

    if (marker == 0x0A || marker == 0x06) {
        marker = reader.ReadUByte(); // consume 0x0A or 0x06
        uint8_t nodeCount = reader.ReadUByte();

        if (nodeCount > 0) {
            uint8_t dataMarker = reader.ReadUByte(); // expect 0x0B or 0x07
            if ((marker == 0x0A && dataMarker != 0x0B) || (marker == 0x06 && dataMarker != 0x07)) {
                SPDLOG_WARN("[BK64:MAP] Expected data marker {} after 0x{:02X}, got "
                            "0x{:02X} at offset 0x{:X} in asset {}",
                            marker == 0x0A ? "0x0B" : "0x07", marker, dataMarker, reader.GetBaseAddress() - 1, symbol);
            }
        }

        for (uint32_t j = 0; j < nodeCount; j++) {
            if (marker == 0x0A) {
                NodeProp nodeProp;
                nodeProp.position[0] = reader.ReadInt16();
                nodeProp.position[1] = reader.ReadInt16();
                nodeProp.position[2] = reader.ReadInt16();

                uint16_t f1 = reader.ReadUInt16();
                nodeProp.radius = (f1 >> 7) & 0x1FF;
                nodeProp.bit6 = (f1 >> 1) & 0x3F;
                nodeProp.bit0 = (f1 >> 0) & 0x01;

                nodeProp.unk8 = reader.ReadUInt16();
                nodeProp.unkA = reader.ReadUByte();
                nodeProp.padB = reader.ReadUByte();

                uint32_t f2 = reader.ReadUInt32();
                nodeProp.yaw = (f2 >> 23) & 0x1FF;
                nodeProp.scale = (f2 >> 0) & 0x7FFFFF;

                uint32_t f3 = reader.ReadUInt32();
                nodeProp.unk10_31 = (f3 >> 20) & 0xFFF;
                nodeProp.unk10_19 = (f3 >> 8) & 0xFFF;
                nodeProp.pad10_7 = (f3 >> 7) & 0x01;
                nodeProp.unk10_6 = (f3 >> 6) & 0x01;
                nodeProp.pad10_5 = (f3 >> 2) & 0x0F;
                nodeProp.unk10_0 = (f3 >> 0) & 0x03;

                cube.nodeProps.push_back(nodeProp);
            } else {
                // OtherNode (0x06 format): 12 bytes. Never appears in a retail ROM;
                // we only handle it so a corrupt/modded ROM doesn't desync the reader.
                uint32_t word0 = reader.ReadUInt32();
                uint32_t word4 = reader.ReadUInt32();
                uint32_t word8 = reader.ReadUInt32();

                // Push a zeroed NodeProp so the array count still lines up
                NodeProp nodeProp = {};
                cube.nodeProps.push_back(nodeProp);

                SPDLOG_ERROR("[BK64:MAP] Encountered OtherNode (0x06 format) at offset "
                             "0x{:X} in asset {}. "
                             "This format is NEVER used in released ROMs and indicates "
                             "ROM corruption or modification. "
                             "Converted to zero-initialized NodeProp, but level data "
                             "is likely broken.",
                             reader.GetBaseAddress() - 12, symbol);
            }
        }
    }

    // Props
    peekPos = reader.GetBaseAddress();
    marker = reader.ReadUByte();
    reader.Seek(peekPos, LUS::SeekOffsetType::Start);

    if (marker == 0x08) {
        reader.ReadUByte(); // consume 0x08
        uint8_t propCount = reader.ReadUByte();

        if (propCount > 0) {
            uint8_t dataMarker = reader.ReadUByte(); // expect 0x09
            if (dataMarker != 0x09) {
                SPDLOG_WARN("[BK64:MAP] Expected data marker 0x09 after 0x08, got "
                            "0x{:02X} at offset 0x{:X} in asset {}",
                            dataMarker, reader.GetBaseAddress() - 1, symbol);
            }
        }

        for (uint32_t j = 0; j < propCount; j++) {
            Prop prop;
            reader.Read((char*)prop.raw, 12);
            cube.props.push_back(prop);
        }
    }
}

// Reads one cube's slot. The gccube wrapper (__code7AF80_initCubeFromFile) loops
// until it hits 0x01, which terminates the cube. Markers seen along the way:
//   0x03 → real cube content (calls code7AF80_initCubeFromFile)
//   0x00 → skip 6 words (two 3-word padding groups)
//   0x02 → skip 3 words
static void ReadCubeData(LUS::BinaryReader& reader, CubeData& cube, const std::string& symbol) {
    bool listed = false;
    while (reader.GetBaseAddress() < reader.GetLength()) {
        uint8_t marker = reader.ReadUByte();

        if (marker == 0x01) {
            // Per-cube terminator; on to the next grid position
            break;
        } else if (marker == 0x03) {
            listed = true;
            ReadCubeContent(reader, cube, symbol);
        } else if (marker == 0x00) {
            // Padding: two groups of 3 words (6 × s32 = 24 bytes)
            reader.Seek(reader.GetBaseAddress() + 24, LUS::SeekOffsetType::Start);
        } else if (marker == 0x02) {
            // One group of 3 words
            reader.Seek(reader.GetBaseAddress() + 12, LUS::SeekOffsetType::Start);
        } else {
            SPDLOG_WARN("[BK64:MAP] Unexpected per-cube marker 0x{:02X} at offset "
                        "0x{:X} in asset {}",
                        marker, reader.GetBaseAddress() - 1, symbol);
            break;
        }
    }
    cube.listedEmpty = listed && cube.nodeProps.empty() && cube.props.empty();
}

static void ParseCubeSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map, size_t totalSize,
                             const std::string& symbol) {
    // The section opens with its own 0x01 sub-marker — not the chunk-type 0x01 the
    // outer loop already ate. Source: file_getNWords_ifExpected(fp, 1, from, 3)
    uint8_t innerMarker = reader.ReadUByte();
    if (innerMarker != 0x01) {
        SPDLOG_WARN("[BK64:MAP] Expected inner marker 0x01 at cube section start, "
                    "got 0x{:02X} at offset 0x{:X} in asset {}",
                    innerMarker, reader.GetBaseAddress() - 1, symbol);
        return;
    }

    // from[0..2] then to[0..2], back to back — to[] has no marker in front of it
    // (file_getNWords reads it unconditionally)
    int32_t from[3], to[3];
    from[0] = reader.ReadInt32();
    from[1] = reader.ReadInt32();
    from[2] = reader.ReadInt32();
    to[0] = reader.ReadInt32();
    to[1] = reader.ReadInt32();
    to[2] = reader.ReadInt32();

    map->mCubeMin[0] = from[0];
    map->mCubeMin[1] = from[1];
    map->mCubeMin[2] = from[2];
    map->mCubeMax[0] = to[0];
    map->mCubeMax[1] = to[1];
    map->mCubeMax[2] = to[2];

    int32_t countX = to[0] - from[0] + 1;
    int32_t countY = to[1] - from[1] + 1;
    int32_t countZ = to[2] - from[2] + 1;

    // A setup with nothing in it can have its min above its max, and then holds no cubes
    int32_t totalCubes = std::max(countX, 0) * std::max(countY, 0) * std::max(countZ, 0);

    SPDLOG_INFO("[BK64:MAP] {} cube section: from ({},{},{}) to ({},{},{}) = {} cubes", symbol, from[0], from[1],
                from[2], to[0], to[1], to[2], totalCubes);

    // Same nesting as the source: X outer, Z inner. Order matters — cubes are
    // stored in this sequence, so don't reshuffle the loops.
    for (int32_t x = from[0]; x <= to[0]; x++) {
        for (int32_t y = from[1]; y <= to[1]; y++) {
            for (int32_t z = from[2]; z <= to[2]; z++) {
                CubeData cube;
                cube.x = x;
                cube.y = y;
                cube.z = z;
                // Runs until it eats the 0x01 per-cube terminator
                ReadCubeData(reader, cube, symbol);

                SPDLOG_INFO("[BK64:MAP] {} cube ({},{},{}) nodeProps={} props={}", symbol, x, y, z,
                            cube.nodeProps.size(), cube.props.size());

                map->mCubes.push_back(cube);
            }
        }
    }

    // Section closes with a 0x00 (file_isNextByteExpected(fp, 0) in cubeList_fromFile)
    size_t peekPos = reader.GetBaseAddress();
    uint8_t endMarker = reader.ReadUByte();
    reader.Seek(peekPos, LUS::SeekOffsetType::Start);

    if (endMarker != 0x00) {
        SPDLOG_WARN("[BK64:MAP] Expected end marker 0x00 after {} cubes, got "
                    "0x{:02X} at offset 0x{:X} in asset {}",
                    totalCubes, endMarker, reader.GetBaseAddress(), symbol);
    } else {
        reader.ReadUByte(); // consume 0x00
    }
}

// Camera node section (chunk type 0x03)
static void ParseCameraSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map, const std::string& symbol) {
    // ncCameraNodeList_fromFile format:
    //   while(next byte != 0x00):
    //     file_getShort_ifExpected(fp, 0x01, &index)  → marker 0x01 + s16
    //     file_getByte_ifExpected(fp, 0x02, &type)    → marker 0x02 + u8
    //     cameraNodeTypeN_fromFile(fp, this):
    //       inner sub-loop until 0x00, each field group prefixed by its own
    //       marker byte
    while (reader.GetBaseAddress() + 1 < reader.GetLength()) {
        // Peek for the section terminator
        size_t peekPos = reader.GetBaseAddress();
        uint8_t peekMarker = reader.ReadUByte();
        reader.Seek(peekPos, LUS::SeekOffsetType::Start);

        if (peekMarker == 0x00) {
            reader.ReadUByte(); // eat the section terminator
            break;
        }

        // Each node starts with 0x01
        uint8_t nodeMarker = reader.ReadUByte();
        if (nodeMarker != 0x01) {
            SPDLOG_WARN("[BK64:MAP] Expected node marker 0x01, got 0x{:02X} at "
                        "offset 0x{:X} in asset {}",
                        nodeMarker, reader.GetBaseAddress() - 1, symbol);
            break;
        }

        CameraNode node;
        node.index = reader.ReadInt16();

        // 0x02 marker, then the type byte
        uint8_t typeMarker = reader.ReadUByte();
        if (typeMarker != 0x02) {
            SPDLOG_WARN("[BK64:MAP] Expected type marker 0x02, got 0x{:02X} at "
                        "offset 0x{:X} in asset {}",
                        typeMarker, reader.GetBaseAddress() - 1, symbol);
            break;
        }
        node.type = reader.ReadUByte();

        // Inner sub-loop (cameraNodeTypeN_fromFile). Every type has its own set of
        // sub-markers and ends at 0x00 — except type 0, which has no inner data and
        // no 0x00 terminator at all.
        //
        // Type 1: 0x01→position[3]  0x02→hSpeed+vSpeed  0x03→rotation+accel
        //         0x04→pitchYawRoll[3]  0x05→unknownFlag
        // Type 2: 0x01→position[3]  0x02→pitchYawRoll[3]
        // Type 3: 0x01→position[3]  0x02→hSpeed+vSpeed  0x03→rotation+accel
        //         0x06→closeDist+farDist  0x04→pitchYawRoll[3]  0x05→unknownFlag
        // Type 4: 0x01→unknownFlag
        bool innerError = false;
        if (node.type == 0) {
            // Nothing to read; fall straight through to the push
        } else
            while (!innerError && reader.GetBaseAddress() < reader.GetLength()) {
                uint8_t sub = reader.ReadUByte();
                if (sub == 0x00)
                    break;

                switch (node.type) {
                    case 1:
                        switch (sub) {
                            case 0x01:
                                node.data.type1.position[0] = reader.ReadFloat();
                                node.data.type1.position[1] = reader.ReadFloat();
                                node.data.type1.position[2] = reader.ReadFloat();
                                break;
                            case 0x02:
                                node.data.type1.horizontalSpeed = reader.ReadFloat();
                                node.data.type1.verticalSpeed = reader.ReadFloat();
                                break;
                            case 0x03:
                                node.data.type1.rotation = reader.ReadFloat();
                                node.data.type1.accelaration = reader.ReadFloat();
                                break;
                            case 0x04:
                                node.data.type1.pitchYawRoll[0] = reader.ReadFloat();
                                node.data.type1.pitchYawRoll[1] = reader.ReadFloat();
                                node.data.type1.pitchYawRoll[2] = reader.ReadFloat();
                                break;
                            case 0x05:
                                node.data.type1.unknownFlag = reader.ReadInt32();
                                break;
                            default:
                                SPDLOG_WARN("[BK64:MAP] Unknown sub-marker 0x{:02X} in type1 "
                                            "camera node at 0x{:X} in {}",
                                            sub, reader.GetBaseAddress() - 1, symbol);
                                innerError = true;
                        }
                        break;

                    case 2:
                        switch (sub) {
                            case 0x01:
                                node.data.type2.position[0] = reader.ReadFloat();
                                node.data.type2.position[1] = reader.ReadFloat();
                                node.data.type2.position[2] = reader.ReadFloat();
                                break;
                            case 0x02:
                                node.data.type2.pitchYawRoll[0] = reader.ReadFloat();
                                node.data.type2.pitchYawRoll[1] = reader.ReadFloat();
                                node.data.type2.pitchYawRoll[2] = reader.ReadFloat();
                                break;
                            default:
                                SPDLOG_WARN("[BK64:MAP] Unknown sub-marker 0x{:02X} in type2 "
                                            "camera node at 0x{:X} in {}",
                                            sub, reader.GetBaseAddress() - 1, symbol);
                                innerError = true;
                        }
                        break;

                    case 3:
                        switch (sub) {
                            case 0x01:
                                node.data.type3.position[0] = reader.ReadFloat();
                                node.data.type3.position[1] = reader.ReadFloat();
                                node.data.type3.position[2] = reader.ReadFloat();
                                break;
                            case 0x02:
                                node.data.type3.horizontalSpeed = reader.ReadFloat();
                                node.data.type3.verticalSpeed = reader.ReadFloat();
                                break;
                            case 0x03:
                                node.data.type3.rotation = reader.ReadFloat();
                                node.data.type3.accelaration = reader.ReadFloat();
                                break;
                            case 0x06:
                                node.data.type3.closeDistance = reader.ReadFloat();
                                node.data.type3.farDistance = reader.ReadFloat();
                                break;
                            case 0x04:
                                node.data.type3.pitchYawRoll[0] = reader.ReadFloat();
                                node.data.type3.pitchYawRoll[1] = reader.ReadFloat();
                                node.data.type3.pitchYawRoll[2] = reader.ReadFloat();
                                break;
                            case 0x05:
                                node.data.type3.unknownFlag = reader.ReadInt32();
                                break;
                            default:
                                SPDLOG_WARN("[BK64:MAP] Unknown sub-marker 0x{:02X} in type3 "
                                            "camera node at 0x{:X} in {}",
                                            sub, reader.GetBaseAddress() - 1, symbol);
                                innerError = true;
                        }
                        break;

                    case 4:
                        switch (sub) {
                            case 0x01:
                                node.data.type4.unknownFlag = reader.ReadInt32();
                                break;
                            default:
                                SPDLOG_WARN("[BK64:MAP] Unknown sub-marker 0x{:02X} in type4 "
                                            "camera node at 0x{:X} in {}",
                                            sub, reader.GetBaseAddress() - 1, symbol);
                                innerError = true;
                        }
                        break;

                    default:
                        SPDLOG_WARN("[BK64:MAP] Unknown camera node type {} at offset 0x{:X} "
                                    "in asset {}",
                                    node.type, reader.GetBaseAddress(), symbol);
                        innerError = true;
                }
            }

        map->mCameraNodes.push_back(node);
        SPDLOG_INFO("[BK64:MAP] {} camera node index={} type={}", symbol, node.index, node.type);

        if (innerError)
            break;
    }
    // Terminator's already eaten, so the outer loop lands on the next chunk.
    SPDLOG_INFO("[BK64:MAP] {} camera section: {} nodes", symbol, map->mCameraNodes.size());
}

// Lighting vector section (chunk type 0x04)
static void ParseLightingSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map, const std::string& symbol) {
    // Each entry: marker 0x01 + position (f32[3]) + fade_radii (f32[2]) +
    // rgb (s32[3]). Keep going until we hit a chunk marker (0x00-0x04).
    while (reader.GetBaseAddress() + 1 < reader.GetLength()) {
        // Peek without consuming so we can spot a chunk marker
        size_t peekPos = reader.GetBaseAddress();
        uint8_t peekMarker = reader.ReadUByte();
        reader.Seek(peekPos, LUS::SeekOffsetType::Start);

        // 0x00 ends the section; 0x01 starts an entry. Mirrors
        // lightingVectorList_fromFile: while(!file_isNextByteExpected(fp, 0)).
        if (peekMarker == 0x00) {
            reader.ReadUByte(); // eat the terminator
            break;
        }

        uint8_t marker = reader.ReadUByte();

        if (marker == 0x01) {
            LightingVector light;

            // position
            uint8_t posMarker = reader.ReadUByte();
            if (posMarker != 0x02) {
                SPDLOG_WARN("[BK64:MAP] Expected position marker 0x02, got 0x{:02X} at "
                            "offset 0x{:X} in asset {}",
                            posMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            light.position[0] = reader.ReadFloat();
            light.position[1] = reader.ReadFloat();
            light.position[2] = reader.ReadFloat();

            // fade radii
            uint8_t fadeMarker = reader.ReadUByte();
            if (fadeMarker != 0x03) {
                SPDLOG_WARN("[BK64:MAP] Expected fade marker 0x03, got 0x{:02X} at "
                            "offset 0x{:X} in asset {}",
                            fadeMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            light.fadeRadii[0] = reader.ReadFloat();
            light.fadeRadii[1] = reader.ReadFloat();

            // rgb
            uint8_t rgbMarker = reader.ReadUByte();
            if (rgbMarker != 0x04) {
                SPDLOG_WARN("[BK64:MAP] Expected RGB marker 0x04, got 0x{:02X} at "
                            "offset 0x{:X} in asset {}",
                            rgbMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            light.rgb[0] = reader.ReadInt32();
            light.rgb[1] = reader.ReadInt32();
            light.rgb[2] = reader.ReadInt32();

            map->mLightingVectors.push_back(light);
            SPDLOG_INFO("[BK64:MAP] {} light pos=({:.2f},{:.2f},{:.2f}) "
                        "radii=({:.2f},{:.2f}) rgb=({},{},{})",
                        symbol, light.position[0], light.position[1], light.position[2], light.fadeRadii[0],
                        light.fadeRadii[1], light.rgb[0], light.rgb[1], light.rgb[2]);
        } else {
            SPDLOG_WARN("[BK64:MAP] Unexpected marker 0x{:02X} in lighting section "
                        "at offset 0x{:X} in asset {}",
                        marker, reader.GetBaseAddress() - 1, symbol);
            break;
        }
    }
    // Terminator's already eaten, so the outer loop lands on the next chunk.
    SPDLOG_INFO("[BK64:MAP] {} lighting section: {} vectors", symbol, map->mLightingVectors.size());
}

std::vector<uint8_t> WriteMapRom(const MapData& map) {
    std::vector<uint8_t> out;
    const auto u8 = [&](uint32_t value) { out.push_back((uint8_t)value); };
    const auto u16 = [&](uint32_t value) {
        u8(value >> 8);
        u8(value);
    };
    const auto u32 = [&](uint32_t value) {
        u16(value >> 16);
        u16(value);
    };
    const auto f32 = [&](float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    };
    const auto f32s = [&](const float* values, int count) {
        for (int i = 0; i < count; i++) {
            f32(values[i]);
        }
    };

    u8(0x01);
    u8(0x01);
    for (const int32_t bound : map.mCubeMin) {
        u32(bound);
    }
    for (const int32_t bound : map.mCubeMax) {
        u32(bound);
    }
    for (const auto& cube : map.mCubes) {
        if (cube.listedEmpty || !cube.nodeProps.empty() || !cube.props.empty()) {
            u8(0x03);
            u8(0x0A);
            u8((uint32_t)cube.nodeProps.size());
            if (!cube.nodeProps.empty()) {
                u8(0x0B);
            }
            for (const auto& node : cube.nodeProps) {
                uint32_t words[5];
                NodeToWords(node, words);
                for (const uint32_t word : words) {
                    u32(word);
                }
            }
            u8(0x08);
            u8((uint32_t)cube.props.size());
            if (!cube.props.empty()) {
                u8(0x09);
            }
            for (const auto& prop : cube.props) {
                out.insert(out.end(), prop.raw, prop.raw + sizeof(prop.raw));
            }
        }
        u8(0x01);
    }
    u8(0x00);

    // Fields go in sub-marker order, as every retail setup has them
    u8(0x03);
    for (const auto& cam : map.mCameraNodes) {
        u8(0x01);
        u16(cam.index);
        u8(0x02);
        u8(cam.type);
        switch (cam.type) {
            case 1:
                u8(0x01);
                f32s(cam.data.type1.position, 3);
                u8(0x02);
                f32(cam.data.type1.horizontalSpeed);
                f32(cam.data.type1.verticalSpeed);
                u8(0x03);
                f32(cam.data.type1.rotation);
                f32(cam.data.type1.accelaration);
                u8(0x04);
                f32s(cam.data.type1.pitchYawRoll, 3);
                u8(0x05);
                u32(cam.data.type1.unknownFlag);
                break;
            case 2:
                u8(0x01);
                f32s(cam.data.type2.position, 3);
                u8(0x02);
                f32s(cam.data.type2.pitchYawRoll, 3);
                break;
            case 3:
                u8(0x01);
                f32s(cam.data.type3.position, 3);
                u8(0x02);
                f32(cam.data.type3.horizontalSpeed);
                f32(cam.data.type3.verticalSpeed);
                u8(0x03);
                f32(cam.data.type3.rotation);
                f32(cam.data.type3.accelaration);
                u8(0x04);
                f32s(cam.data.type3.pitchYawRoll, 3);
                u8(0x05);
                u32(cam.data.type3.unknownFlag);
                u8(0x06);
                f32(cam.data.type3.closeDistance);
                f32(cam.data.type3.farDistance);
                break;
            case 4:
                u8(0x01);
                u32(cam.data.type4.unknownFlag);
                break;
        }
        // Type 0 has no fields and no terminator
        if (cam.type != 0) {
            u8(0x00);
        }
    }
    u8(0x00);

    u8(0x04);
    for (const auto& light : map.mLightingVectors) {
        u8(0x01);
        u8(0x02);
        f32s(light.position, 3);
        u8(0x03);
        f32s(light.fadeRadii, 2);
        u8(0x04);
        for (const int32_t channel : light.rgb) {
            u32(channel);
        }
    }
    u8(0x00);

    u8(0x00);
    return out;
}

std::shared_ptr<MapData> ReadMapResource(const std::vector<uint8_t>& resource) {
    try {
        ResourceReader reader(resource);
        const auto read = [&](auto& value) { reader.Read(value); };
        const auto reads = [&](auto* values, int count) { reader.ReadArray(values, count); };
        auto map = std::make_shared<MapData>();
        uint32_t cubeCount;
        read(cubeCount);
        reads(map->mCubeMin, 3);
        reads(map->mCubeMax, 3);
        const int32_t spanY = map->mCubeMax[1] - map->mCubeMin[1] + 1;
        const int32_t spanZ = map->mCubeMax[2] - map->mCubeMin[2] + 1;
        for (uint32_t i = 0; i < cubeCount; i++) {
            CubeData cube;
            // The header keeps 5 bits of each coordinate, so they come from the cube's place in the grid
            uint32_t header;
            read(header);
            cube.x = map->mCubeMin[0] + (int32_t)i / (spanY * spanZ);
            cube.y = map->mCubeMin[1] + (int32_t)i / spanZ % spanY;
            cube.z = map->mCubeMin[2] + (int32_t)i % spanZ;

            uint32_t nodeCount;
            read(nodeCount);
            for (uint32_t n = 0; n < nodeCount; n++) {
                NodeProp node;
                uint16_t radius, yaw, uid, next;
                uint8_t category, waypoint, pad10_7, unk10_6, pad10_5, unk10_0;
                uint32_t scale;
                reads(node.position, 3);
                read(radius);
                read(category);
                read(waypoint);
                read(node.unk8);
                read(node.unkA);
                read(node.padB);
                read(yaw);
                read(scale);
                read(uid);
                read(next);
                read(pad10_7);
                read(unk10_6);
                read(pad10_5);
                read(unk10_0);
                node.radius = radius;
                node.bit6 = category;
                node.bit0 = waypoint;
                node.yaw = yaw;
                node.scale = scale;
                node.unk10_31 = uid;
                node.unk10_19 = next;
                node.pad10_7 = pad10_7;
                node.unk10_6 = unk10_6;
                node.pad10_5 = pad10_5;
                node.unk10_0 = unk10_0;
                cube.nodeProps.push_back(node);
            }

            uint32_t propCount;
            read(propCount);
            for (uint32_t p = 0; p < propCount; p++) {
                // Kept in the ROM's byte order
                Prop prop;
                const std::vector<uint8_t> raw = reader.Bytes(sizeof(prop.raw));
                std::memcpy(prop.raw, raw.data(), sizeof(prop.raw));
                cube.props.push_back(prop);
            }
            map->mCubes.push_back(cube);
        }

        uint32_t cameraCount;
        read(cameraCount);
        for (uint32_t i = 0; i < cameraCount; i++) {
            CameraNode cam = {};
            read(cam.index);
            read(cam.type);
            switch (cam.type) {
                case 1:
                    reads(cam.data.type1.position, 3);
                    read(cam.data.type1.horizontalSpeed);
                    read(cam.data.type1.verticalSpeed);
                    read(cam.data.type1.rotation);
                    read(cam.data.type1.accelaration);
                    reads(cam.data.type1.pitchYawRoll, 3);
                    read(cam.data.type1.unknownFlag);
                    break;
                case 2:
                    reads(cam.data.type2.position, 3);
                    reads(cam.data.type2.pitchYawRoll, 3);
                    break;
                case 3:
                    reads(cam.data.type3.position, 3);
                    read(cam.data.type3.horizontalSpeed);
                    read(cam.data.type3.verticalSpeed);
                    read(cam.data.type3.rotation);
                    read(cam.data.type3.accelaration);
                    read(cam.data.type3.closeDistance);
                    read(cam.data.type3.farDistance);
                    reads(cam.data.type3.pitchYawRoll, 3);
                    read(cam.data.type3.unknownFlag);
                    break;
                case 4:
                    read(cam.data.type4.unknownFlag);
                    break;
            }
            map->mCameraNodes.push_back(cam);
        }

        uint32_t lightCount;
        read(lightCount);
        for (uint32_t i = 0; i < lightCount; i++) {
            LightingVector light;
            reads(light.position, 3);
            reads(light.fadeRadii, 2);
            reads(light.rgb, 3);
            map->mLightingVectors.push_back(light);
        }
        return map;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[BK64:MAP] {}", e.what());
        return nullptr;
    }
}

struct Field {
    const char* key;
    uint8_t word;
    uint8_t shift;
    uint8_t width;
    char form = 'd';       // d decimal, x hex, b true/false, 2 degrees stored halved, a the apply list
    uint32_t fallback = 0; // left out of the yaml when it holds this, and UINT32_MAX makes it required
    uint32_t offset = 0;   // added for the yaml, so an id reads as its asset ID
};

static uint32_t FieldMask(const Field& field) {
    return field.width >= 32 ? 0xFFFFFFFF : (1u << field.width) - 1;
}

static const Field kNodeId[] = { { "Id", 2, 16, 16, 'x', UINT32_MAX } };
static const Field kNodeFields[] = {
    { "Radius", 1, 7, 9 },
    { "Yaw", 3, 23, 9 },
    { "Scale", 3, 0, 23 },
    { "Marker", 2, 8, 8 },
    { "Number", 4, 20, 12 },
    { "Next", 4, 8, 12 },
    { "padB", 2, 0, 8, 'x' },
    { "pad10_7", 4, 7, 1 },
    // The game sets it on every node as the setup loads
    { "unk10_6", 4, 6, 1, 'd', 1 },
    { "pad10_5", 4, 2, 4 },
};
static const Field kZoneSetting[] = { { "ActiveWhen", 4, 0, 2 } };
static const Field kNodeSetting[] = { { "unk10_0", 4, 0, 2 } };

static const Field kWaypointLinks[] = { { "Number", 4, 20, 12 }, { "Next", 4, 8, 12 } };
// Struct_glspline_t1 in spline_pathfollow.c.
static const Field kLegFields[] = {
    { "Apply", 2, 8, 3, 'a' }, { "Pause", 3, 1, 11 },          { "WaitsForCue", 4, 5, 1, 'b' },
    { "Speed", 3, 12, 11 },    { "Anim", 2, 22, 10, 'x' },     { "AnimDuration", 2, 11, 11 },
    { "AnimMode", 1, 1, 3 },   { "HeadingMode", 1, 4, 3 },     { "Yaw", 3, 23, 9 },
    { "Pitch", 1, 7, 9 },      { "Link", 1, 20, 12 },          { "Blend", 1, 16, 2 },
    { "TurnSpeed", 1, 18, 2 }, { "SmoothTurn", 4, 6, 1, 'b' }, { "Backward", 4, 0, 1, 'b' },
    { "pad8", 2, 0, 8, 'x' },  { "pad10_7", 4, 7, 1 },         { "pad10_5", 4, 2, 3 },
    { "unk10_1", 4, 1, 1 },
};
// Struct_glspline_t0. Arg is a track's volume or a sound's sample rate, over 8; Volume is a sound's, 0x800000 = 1.0.
static const Field kEventFields[] = {
    { "Value", 1, 16, 16, 'x' }, { "Arg", 1, 4, 12 },          { "Volume", 2, 8, 24, 'x' },
    { "Slot", 4, 1, 7 },         { "Dialog", 3, 21, 1, 'b' },  { "DialogText", 3, 13, 8, 'x' },
    { "JumpTo", 3, 1, 12 },      { "Backward", 4, 0, 1, 'b' }, { "pad8", 2, 0, 8, 'x' },
    { "unkC_23", 3, 22, 2 },     { "padC", 3, 24, 8, 'x' },
};

static const Field kSpriteId[] = { { "Sprite", 0, 20, 12, 'x', UINT32_MAX, SPRITE_ASSET_OFFSET } };
static const Field kSpriteFields[] = {
    { "Scale", 0, 2, 8 },    { "Frame", 2, 11, 5 },
    { "Phase", 2, 6, 5 },    { "Mirrored", 0, 1, 1, 'b' },
    { "unk8_5", 2, 5, 1 },   { "isNotFeatherEggOrNote", 2, 4, 1 },
    { "unk8_3", 2, 3, 1 },   { "isCollisionResolved", 2, 2, 1 },
    { "unk0_19", 0, 19, 1 }, { "pad0_0", 0, 0, 1 },
};
static const Field kModelId[] = { { "Model", 0, 20, 12, 'x', UINT32_MAX, MODEL_ASSET_OFFSET } };
static const Field kModelFields[] = {
    { "Yaw", 0, 8, 8, '2' }, { "Roll", 0, 0, 8, '2' },      { "Scale", 2, 8, 8 },  { "padB_7", 2, 6, 2 },
    { "unkB_5", 2, 5, 1 },   { "unkB_4", 2, 4, 1, 'd', 1 }, { "unk8_3", 2, 3, 1 }, { "isCollisionResolved", 2, 2, 1 },
    { "pad0_19", 0, 16, 4 },
};
static const Field kActorSlotFields[] = {
    { "Frame", 2, 11, 5 },        { "Phase", 2, 6, 5 },
    { "Mirrored", 2, 5, 1, 'b' }, { "isNotFeatherEggOrNote", 2, 4, 1 },
    { "unk8_3", 2, 3, 1 },        { "isCollisionResolved", 2, 2, 1 },
    { "isModelProp", 2, 1, 1 },   { "Marker", 0, 0, 32, 'x' },
};
// rgb_remove_red, _green and _blue.
static const Field kSpriteColor[] = { { "", 0, 16, 3 }, { "", 0, 13, 3 }, { "", 0, 10, 3 } };

template <size_t N> static void EmitFields(YAML::Emitter& out, const uint32_t* words, const Field (&fields)[N]) {
    for (const Field& field : fields) {
        const uint32_t value = (words[field.word] >> field.shift) & FieldMask(field);
        if (value == field.fallback) {
            continue;
        }
        out << YAML::Key << field.key << YAML::Value;
        switch (field.form) {
            case 'x':
                out << YAML_HEX(value + field.offset);
                break;
            case 'b':
                out << (value != 0);
                break;
            case '2':
                out << value * 2;
                break;
            case 'a':
                out << YAML::Flow << YAML::BeginSeq;
                for (int bit = 0; bit < 3; bit++) {
                    if (value & (1 << bit)) {
                        out << sApplyNames[bit];
                    }
                }
                out << YAML::EndSeq;
                break;
            default:
                out << value + field.offset;
        }
    }
}

static void BeginList(YAML::Emitter& out, bool empty) {
    if (empty) {
        out << YAML::Flow;
    }
    out << YAML::BeginSeq;
}

static void EmitCell(YAML::Emitter& out, const CubeData& cube) {
    const int32_t cell[3] = { cube.x, cube.y, cube.z };
    EmitList(out, "Cell", cell, 3);
}

// Nodes and props carry their cell only when their position doesn't map to it.
static bool InItsCell(const MapData& map, const CubeData& cube, const int16_t position[3]) {
    const int32_t cell[3] = { cube.x, cube.y, cube.z };
    for (int axis = 0; axis < 3; axis++) {
        if (CellOf(position[axis], map.mCubeMin[axis], map.mCubeMax[axis]) != cell[axis]) {
            return false;
        }
    }
    return true;
}

std::string MapToYaml(const MapData& map, const std::string& symbol) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);
    out << YAML::BeginMap;

    out << YAML::Key << "Bounds" << YAML::Value << YAML::Flow << YAML::BeginMap;
    EmitList(out, "Min", map.mCubeMin, 3);
    EmitList(out, "Max", map.mCubeMax, 3);
    out << YAML::EndMap;

    std::vector<const CubeData*> empty;
    for (const auto& cube : map.mCubes) {
        if (cube.listedEmpty) {
            empty.push_back(&cube);
        }
    }
    if (!empty.empty()) {
        out << YAML::Key << "EmptyCells" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (const CubeData* cube : empty) {
            const int32_t cell[3] = { cube->x, cube->y, cube->z };
            out << YAML::Flow << YAML::BeginSeq << cell[0] << cell[1] << cell[2] << YAML::EndSeq;
        }
        out << YAML::EndSeq;
    }

    out << YAML::Key << "Nodes" << YAML::Value;
    BeginList(out, std::all_of(map.mCubes.begin(), map.mCubes.end(),
                               [](const auto& cube) { return cube.nodeProps.empty(); }));
    for (const auto& cube : map.mCubes) {
        for (const auto& node : cube.nodeProps) {
            uint32_t words[5];
            NodeToWords(node, words);
            out << YAML::Flow << YAML::BeginMap;
            if (node.bit0) {
                const bool event = words[3] & 1;
                const uint8_t kind = (words[1] >> 1) & 7;
                float fraction;
                std::memcpy(&fraction, &words[0], sizeof(fraction));
                out << YAML::Key << "Category" << YAML::Value << "Waypoint";
                if (event) {
                    out << YAML::Key << "Event" << YAML::Value;
                    if (sEventKinds[kind]) {
                        out << sEventKinds[kind];
                    } else {
                        out << (int)kind;
                    }
                }
                EmitFields(out, words, kWaypointLinks);
                out << YAML::Key << "Fraction" << YAML::Value << fraction;
                EmitCell(out, cube);
                if (event) {
                    EmitFields(out, words, kEventFields);
                } else {
                    EmitFields(out, words, kLegFields);
                }
            } else {
                out << YAML::Key << "Category" << YAML::Value;
                if (const char* name = NodeCategoryName(node.bit6); *name) {
                    out << name;
                } else {
                    out << (int)node.bit6;
                }
                EmitFields(out, words, kNodeId);
                EmitList(out, "Position", node.position, 3);
                if (!InItsCell(map, cube, node.position)) {
                    EmitCell(out, cube);
                }
                EmitFields(out, words, kNodeFields);
                if (node.bit6 == CATEGORY_CAMERA_ZONE) {
                    EmitFields(out, words, kZoneSetting);
                } else {
                    EmitFields(out, words, kNodeSetting);
                }
            }
            out << YAML::EndMap;
        }
    }
    out << YAML::EndSeq;

    out << YAML::Key << "Props" << YAML::Value;
    BeginList(out,
              std::all_of(map.mCubes.begin(), map.mCubes.end(), [](const auto& cube) { return cube.props.empty(); }));
    for (const auto& cube : map.mCubes) {
        for (const auto& prop : cube.props) {
            const PropKind kind = KindOf(prop);
            uint32_t words[3];
            PropToWords(prop, words);
            const int16_t position[3] = { PropPosition(prop, 0), PropPosition(prop, 1), PropPosition(prop, 2) };
            out << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "Type" << YAML::Value << sPropKinds[kind];
            if (kind == PROP_SPRITE) {
                EmitFields(out, words, kSpriteId);
            } else if (kind == PROP_MODEL) {
                EmitFields(out, words, kModelId);
            }
            EmitList(out, "Position", position, 3);
            if (!InItsCell(map, cube, position)) {
                EmitCell(out, cube);
            }
            if (kind == PROP_SPRITE) {
                EmitFields(out, words, kSpriteFields);
                int color[3];
                for (int i = 0; i < 3; i++) {
                    color[i] = (words[0] >> kSpriteColor[i].shift) & 7;
                }
                if (color[0] || color[1] || color[2]) {
                    EmitList(out, "ColorRemove", color, 3);
                }
            } else if (kind == PROP_MODEL) {
                EmitFields(out, words, kModelFields);
            } else {
                EmitFields(out, words, kActorSlotFields);
            }
            out << YAML::EndMap;
        }
    }
    out << YAML::EndSeq;

    out << YAML::Key << "Cameras" << YAML::Value;
    BeginList(out, map.mCameraNodes.empty());
    for (const auto& cam : map.mCameraNodes) {
        out << YAML::Flow << YAML::BeginMap;
        out << YAML::Key << "Index" << YAML::Value << cam.index;
        out << YAML::Key << "Type" << YAML::Value;
        if (cam.type < std::size(sCameraTypes)) {
            out << sCameraTypes[cam.type];
        } else {
            out << (int)cam.type;
        }
        const auto speeds = [&](const auto& data) {
            EmitList(out, "Position", data.position, 3);
            out << YAML::Key << "HorizontalSpeed" << YAML::Value << data.horizontalSpeed;
            out << YAML::Key << "VerticalSpeed" << YAML::Value << data.verticalSpeed;
            out << YAML::Key << "Rotation" << YAML::Value << data.rotation;
            out << YAML::Key << "Acceleration" << YAML::Value << data.accelaration;
        };
        switch (cam.type) {
            case 1:
                speeds(cam.data.type1);
                EmitList(out, "LooksFrom", cam.data.type1.pitchYawRoll, 3);
                out << YAML::Key << "Flags" << YAML::Value << YAML_HEX((uint32_t)cam.data.type1.unknownFlag);
                break;
            case 2:
                EmitList(out, "Position", cam.data.type2.position, 3);
                EmitList(out, "PitchYawRoll", cam.data.type2.pitchYawRoll, 3);
                break;
            case 3:
                speeds(cam.data.type3);
                out << YAML::Key << "CloseDistance" << YAML::Value << cam.data.type3.closeDistance;
                out << YAML::Key << "FarDistance" << YAML::Value << cam.data.type3.farDistance;
                EmitList(out, "LooksFrom", cam.data.type3.pitchYawRoll, 3);
                out << YAML::Key << "Flags" << YAML::Value << YAML_HEX((uint32_t)cam.data.type3.unknownFlag);
                break;
            case 4:
                out << YAML::Key << "Preset" << YAML::Value << cam.data.type4.unknownFlag;
                break;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "Lights" << YAML::Value;
    BeginList(out, map.mLightingVectors.empty());
    for (const auto& light : map.mLightingVectors) {
        out << YAML::Flow << YAML::BeginMap;
        EmitList(out, "Position", light.position, 3);
        EmitList(out, "Fade", light.fadeRadii, 2);
        EmitList(out, "Color", light.rgb, 3);
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::EndMap;
    out << YAML::EndMap;
    return out.c_str();
}

} // namespace BK64
