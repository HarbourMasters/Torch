#include "GeoLayoutFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include <cstring>
#include <deque>

#define ALIGN8(val) (((val) + 7) & ~7)
#define YAML_HEX(num) YAML::Hex << (num) << YAML::Dec

namespace BK64 {

ExportResult GeoLayoutHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    return std::nullopt;
}

ExportResult GeoLayoutCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto geo = std::static_pointer_cast<GeoLayoutData>(raw);

    return offset;
}

// Serialized size of one geo command, 8-byte header included.
static uint32_t GetGeoCommandByteSize(const GeoLayoutCommand& cmd) {
    uint32_t bodySize = 0;
    switch (cmd.opCode) {
        case GeoLayoutOpCode::UnknownCmd0:
            bodySize = 16;
            break; // 2+2+4+4+4
        case GeoLayoutOpCode::Sort:
            bodySize = 32;
            break; // 4*6+2+2+4 (matches GeoCmd1)
        case GeoLayoutOpCode::Bone:
            bodySize = 4;
            break; // 1+1+2
        case GeoLayoutOpCode::LoadDL:
            bodySize = 4;
            break; // 2+2
        case GeoLayoutOpCode::Skinning:
            // 2 per arg + 2 for terminator
            bodySize = static_cast<uint32_t>(cmd.args.size()) * 2 + 2;
            break;
        case GeoLayoutOpCode::Branch:
            bodySize = 4;
            break; // 4
        case GeoLayoutOpCode::UnknownCmd7:
            bodySize = 4;
            break; // 2+2
        case GeoLayoutOpCode::LOD:
            bodySize = 24;
            break; // 4*5+4
        case GeoLayoutOpCode::ReferencePoint:
            bodySize = 16;
            break; // 2+2+4+4+4
        case GeoLayoutOpCode::Selector:
            // 2+2 + (args.size()-2)*4
            bodySize = 4 + static_cast<uint32_t>(cmd.args.size() - 2) * 4;
            break;
        case GeoLayoutOpCode::DrawDistance:
            bodySize = 16;
            break; // 2*8
        case GeoLayoutOpCode::UnknownCmdE:
            bodySize = 12;
            break; // 2*6
        case GeoLayoutOpCode::UnknownCmdF:
            bodySize = 16;
            break; // 2+1+1+12
        case GeoLayoutOpCode::UnknownCmd10:
            bodySize = 4;
            break; // 4
        default:
            break;
    }
    return 8 + bodySize; // 8 = opcode(4) + cmdLength(4)
}

ExportResult BK64::GeoLayoutBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                   std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto geo = std::static_pointer_cast<GeoLayoutData>(raw);

    // Size the buffer from each command's original offset plus its length.
    uint32_t totalSize = 0;
    for (const auto& cmd : geo->mCmds) {
        uint32_t end = cmd.originalOffset + GetGeoCommandByteSize(cmd);
        if (end > totalSize)
            totalSize = end;
    }

    // Zero-fill, then drop each command back at the offset it came from.
    std::vector<uint8_t> buffer(totalSize, 0);

    for (const auto& cmd : geo->mCmds) {
        uint32_t pos = cmd.originalOffset;
        const auto& arguments = cmd.args;

        // Little helpers: write at pos, bump pos
        auto writeU8 = [&](uint8_t v) { buffer[pos++] = v; };
        auto writeU16 = [&](uint16_t v) {
            memcpy(&buffer[pos], &v, 2);
            pos += 2;
        };
        auto writeS16 = [&](int16_t v) {
            memcpy(&buffer[pos], &v, 2);
            pos += 2;
        };
        auto writeU32 = [&](uint32_t v) {
            memcpy(&buffer[pos], &v, 4);
            pos += 4;
        };
        auto writeS32 = [&](int32_t v) {
            memcpy(&buffer[pos], &v, 4);
            pos += 4;
        };
        auto writeF32 = [&](float v) {
            memcpy(&buffer[pos], &v, 4);
            pos += 4;
        };

        // Header is opcode then cmdLength
        writeU32(static_cast<uint32_t>(cmd.opCode));
        writeU32(cmd.cmdLength);

        switch (cmd.opCode) {
            case GeoLayoutOpCode::UnknownCmd0:
                writeU16(std::get<uint16_t>(arguments[0]));
                writeU16(std::get<uint16_t>(arguments[1]));
                writeF32(std::get<float>(arguments[2]));
                writeF32(std::get<float>(arguments[3]));
                writeF32(std::get<float>(arguments[4]));
                break;
            case GeoLayoutOpCode::Sort:
                writeF32(std::get<float>(arguments[0]));
                writeF32(std::get<float>(arguments[1]));
                writeF32(std::get<float>(arguments[2]));
                writeF32(std::get<float>(arguments[3]));
                writeF32(std::get<float>(arguments[4]));
                writeF32(std::get<float>(arguments[5]));
                // [port] Decomp reads unk20 as s16, unk22 as s16, unk24 as s32, so
                // we match the GeoCmd1 struct layout here, not the N64 BE byte layout.
                writeS16(static_cast<int16_t>(std::get<uint8_t>(arguments[6])));  // unk20 (layoutOrder)
                writeS16(static_cast<int16_t>(std::get<uint16_t>(arguments[7]))); // unk22 (firstChildOffset)
                writeS32(static_cast<int32_t>(std::get<uint16_t>(arguments[8]))); // unk24 (secondChildOffset)
                break;
            case GeoLayoutOpCode::Bone:
                writeU8(std::get<uint8_t>(arguments[0]));
                writeU8(std::get<uint8_t>(arguments[1]));
                writeU16(std::get<uint16_t>(arguments[2]));
                break;
            case GeoLayoutOpCode::LoadDL:
                writeU16(std::get<uint16_t>(arguments[0]));
                writeU16(std::get<uint16_t>(arguments[1]));
                break;
            case GeoLayoutOpCode::Skinning:
                writeU16(std::get<uint16_t>(arguments[0]));
                for (size_t i = 1; i < arguments.size(); i++)
                    writeU16(std::get<uint16_t>(arguments[i]));
                writeU16(0); // terminator
                break;
            case GeoLayoutOpCode::Branch:
                writeU32(std::get<uint32_t>(arguments[0]));
                break;
            case GeoLayoutOpCode::UnknownCmd7:
                writeU16(0); // pad
                writeU16(std::get<uint16_t>(arguments[0]));
                break;
            case GeoLayoutOpCode::LOD:
                writeF32(std::get<float>(arguments[0]));
                writeF32(std::get<float>(arguments[1]));
                writeF32(std::get<float>(arguments[2]));
                writeF32(std::get<float>(arguments[3]));
                writeF32(std::get<float>(arguments[4]));
                writeU32(std::get<uint32_t>(arguments[5]));
                break;
            case GeoLayoutOpCode::ReferencePoint:
                writeU16(std::get<uint16_t>(arguments[0]));
                writeU16(std::get<uint16_t>(arguments[1]));
                writeF32(std::get<float>(arguments[2]));
                writeF32(std::get<float>(arguments[3]));
                writeF32(std::get<float>(arguments[4]));
                break;
            case GeoLayoutOpCode::Selector:
                writeU16(std::get<uint16_t>(arguments[0]));
                writeU16(std::get<uint16_t>(arguments[1]));
                for (size_t i = 2; i < arguments.size(); i++)
                    writeU32(std::get<uint32_t>(arguments[i]));
                break;
            case GeoLayoutOpCode::DrawDistance:
                writeS16(std::get<int16_t>(arguments[0]));
                writeS16(std::get<int16_t>(arguments[1]));
                writeS16(std::get<int16_t>(arguments[2]));
                writeS16(std::get<int16_t>(arguments[3]));
                writeS16(std::get<int16_t>(arguments[4]));
                writeS16(std::get<int16_t>(arguments[5]));
                writeS16(std::get<int16_t>(arguments[6]));
                writeS16(std::get<int16_t>(arguments[7]));
                break;
            case GeoLayoutOpCode::UnknownCmdE:
                writeS16(std::get<int16_t>(arguments[0]));
                writeS16(std::get<int16_t>(arguments[1]));
                writeS16(std::get<int16_t>(arguments[2]));
                writeS16(std::get<int16_t>(arguments[3]));
                writeS16(std::get<int16_t>(arguments[4]));
                writeS16(std::get<int16_t>(arguments[5]));
                break;
            case GeoLayoutOpCode::UnknownCmdF:
                writeU16(std::get<uint16_t>(arguments[0]));
                writeU8(std::get<uint8_t>(arguments[1]));
                writeU8(std::get<uint8_t>(arguments[2]));
                for (size_t i = 3; i < arguments.size(); i++)
                    writeU8(std::get<uint8_t>(arguments[i]));
                break;
            case GeoLayoutOpCode::UnknownCmd10:
                writeS32(std::get<int32_t>(arguments[0]));
                break;
            default:
                throw std::runtime_error("BK64::GeoLayoutBinaryExporter: Unknown OpCode Found " +
                                         std::to_string(static_cast<uint32_t>(cmd.opCode)));
        }
    }

    LUS::BinaryWriter output = LUS::BinaryWriter();
    WriteHeader(output, Torch::ResourceType::Blob, 0);

    output.Write(static_cast<uint32_t>(buffer.size()));
    output.Write(reinterpret_cast<char*>(buffer.data()), buffer.size());
    output.Finish(write);
    output.Close();

    return std::nullopt;
}

ExportResult GeoLayoutModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto geo = std::static_pointer_cast<GeoLayoutData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);
    out << YAML::BeginSeq;
    std::deque<std::tuple<uint32_t, uint32_t, uint32_t>> childrenStack;

    for (auto& [opCode, cmdLength, arguments, origOff_] : geo->mCmds) {
        uint32_t numChildren = 0;
        uint32_t i = 0;

        out << YAML::Value;
        out << YAML::BeginMap;

        switch (opCode) {
            case GeoLayoutOpCode::UnknownCmd0:
                out << YAML::Key << "UnknownCmd0";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "childOffset" << YAML::Value << YAML_HEX(std::get<uint16_t>(arguments.at(0)));
                out << YAML::Key << "shouldRotatePitch" << YAML::Value << (bool)std::get<uint16_t>(arguments.at(1));
                out << YAML::Key << "x" << YAML::Value << std::get<float>(arguments.at(2));
                out << YAML::Key << "y" << YAML::Value << std::get<float>(arguments.at(3));
                out << YAML::Key << "z" << YAML::Value << std::get<float>(arguments.at(4));
                break;
            case GeoLayoutOpCode::Sort:
                out << YAML::Key << "Sort";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "x1" << YAML::Value << std::get<float>(arguments.at(0));
                out << YAML::Key << "y1" << YAML::Value << std::get<float>(arguments.at(1));
                out << YAML::Key << "z1" << YAML::Value << std::get<float>(arguments.at(2));
                out << YAML::Key << "x2" << YAML::Value << std::get<float>(arguments.at(3));
                out << YAML::Key << "y2" << YAML::Value << std::get<float>(arguments.at(4));
                out << YAML::Key << "z2" << YAML::Value << std::get<float>(arguments.at(5));
                out << YAML::Key << "layoutOrder" << YAML::Value << (uint32_t)std::get<uint8_t>(arguments.at(6));
                out << YAML::Key << "firstChildOffset" << YAML::Value << YAML_HEX(std::get<uint16_t>(arguments.at(7)));
                out << YAML::Key << "secondChildOffset" << YAML::Value << YAML_HEX(std::get<uint16_t>(arguments.at(8)));

                if (std::get<uint16_t>(arguments.at(7)) != 0) {
                    numChildren++;
                }
                if (std::get<uint16_t>(arguments.at(8)) != 0) {
                    numChildren++;
                }
                break;
            case GeoLayoutOpCode::Bone:
                out << YAML::Key << "Bone";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "childOffset" << YAML::Value
                    << YAML_HEX((uint32_t)std::get<uint8_t>(arguments.at(0)));
                out << YAML::Key << "boneId" << YAML::Value << (uint32_t)std::get<uint8_t>(arguments.at(1));
                out << YAML::Key << "unkBoneInfo" << YAML::Value << std::get<uint16_t>(arguments.at(2));
                if (std::get<uint8_t>(arguments.at(0)) != 0) {
                    numChildren++;
                }
                break;
            case GeoLayoutOpCode::LoadDL:
                out << YAML::Key << "LoadDL";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "dlIndex" << YAML::Value << std::get<uint16_t>(arguments.at(0));
                out << YAML::Key << "triCount" << YAML::Value << std::get<uint16_t>(arguments.at(1));
                break;
            case GeoLayoutOpCode::Skinning:
                out << YAML::Key << "Skinning";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "dlOffsetPreviousBone" << YAML::Value << std::get<uint16_t>(arguments.at(0));
                out << YAML::Key << "dlOffsets" << YAML::Value;
                out << YAML::BeginSeq;
                for (size_t j = 1; j < arguments.size(); j++) {
                    out << YAML::Value << std::get<uint16_t>(arguments.at(j));
                }
                out << YAML::EndSeq;
                break;
            case GeoLayoutOpCode::Branch:
                out << YAML::Key << "Branch";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "cmdTargetOffset" << YAML::Value << std::get<uint32_t>(arguments.at(0));
                break;
            case GeoLayoutOpCode::UnknownCmd7:
                out << YAML::Key << "UnknownCmd7";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "dlIndex" << YAML::Value << std::get<uint16_t>(arguments.at(0));
                break;
            case GeoLayoutOpCode::LOD:
                out << YAML::Key << "LOD";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "maxDistance" << YAML::Value << std::get<float>(arguments.at(0));
                out << YAML::Key << "minDistance" << YAML::Value << std::get<float>(arguments.at(1));
                out << YAML::Key << "x" << YAML::Value << std::get<float>(arguments.at(2));
                out << YAML::Key << "y" << YAML::Value << std::get<float>(arguments.at(3));
                out << YAML::Key << "z" << YAML::Value << std::get<float>(arguments.at(4));
                out << YAML::Key << "childOffset" << YAML::Value << YAML_HEX(std::get<uint32_t>(arguments.at(5)));
                if (std::get<uint32_t>(arguments.at(5)) != 0) {
                    numChildren++;
                }
                break;
            case GeoLayoutOpCode::ReferencePoint:
                out << YAML::Key << "ReferencePoint";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "referencePointIndex" << YAML::Value << std::get<uint16_t>(arguments.at(0));
                out << YAML::Key << "boneIndex" << YAML::Value << std::get<uint16_t>(arguments.at(1));
                out << YAML::Key << "boneOffsetX" << YAML::Value << std::get<float>(arguments.at(2));
                out << YAML::Key << "boneOffsetY" << YAML::Value << std::get<float>(arguments.at(3));
                out << YAML::Key << "boneOffsetZ" << YAML::Value << std::get<float>(arguments.at(4));
                break;
            case GeoLayoutOpCode::Selector:
                out << YAML::Key << "Selector";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "childCount" << YAML::Value << std::get<uint16_t>(arguments.at(0));
                out << YAML::Key << "selectorIndex" << YAML::Value << std::get<uint16_t>(arguments.at(1));
                out << YAML::Key << "childOffsets" << YAML::Value;
                out << YAML::BeginSeq;
                for (size_t j = 2; j < arguments.size(); j++) {
                    out << YAML::Value << YAML_HEX(std::get<uint32_t>(arguments.at(j)));
                    if (std::get<uint32_t>(arguments.at(j)) != 0) {
                        numChildren++;
                    }
                }
                out << YAML::EndSeq;
                break;
            case GeoLayoutOpCode::DrawDistance:
                out << YAML::Key << "DrawDistance";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "negX" << YAML::Value << std::get<int16_t>(arguments.at(0));
                out << YAML::Key << "negY" << YAML::Value << std::get<int16_t>(arguments.at(1));
                out << YAML::Key << "negZ" << YAML::Value << std::get<int16_t>(arguments.at(2));
                out << YAML::Key << "posX" << YAML::Value << std::get<int16_t>(arguments.at(3));
                out << YAML::Key << "posY" << YAML::Value << std::get<int16_t>(arguments.at(4));
                out << YAML::Key << "posZ" << YAML::Value << std::get<int16_t>(arguments.at(5));
                out << YAML::Key << "unk14" << YAML::Value << std::get<int16_t>(arguments.at(6));
                out << YAML::Key << "unk16" << YAML::Value << std::get<int16_t>(arguments.at(7));
                break;
            case GeoLayoutOpCode::UnknownCmdE:
                out << YAML::Key << "UnknownCmdE";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "coords1X" << YAML::Value << std::get<int16_t>(arguments.at(0));
                out << YAML::Key << "coords1Y" << YAML::Value << std::get<int16_t>(arguments.at(1));
                out << YAML::Key << "coords1Z" << YAML::Value << std::get<int16_t>(arguments.at(2));
                out << YAML::Key << "coords2X" << YAML::Value << std::get<int16_t>(arguments.at(3));
                out << YAML::Key << "coords2Y" << YAML::Value << std::get<int16_t>(arguments.at(4));
                out << YAML::Key << "coords2Z" << YAML::Value << std::get<int16_t>(arguments.at(5));
                break;
            case GeoLayoutOpCode::UnknownCmdF:
                out << YAML::Key << "UnknownCmdF";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "childOffset" << YAML::Value << YAML_HEX(std::get<uint16_t>(arguments.at(0)));
                out << YAML::Key << "unkA" << YAML::Value << (uint32_t)std::get<uint8_t>(arguments.at(1));
                out << YAML::Key << "unkB" << YAML::Value << (uint32_t)std::get<uint8_t>(arguments.at(2));
                out << YAML::Key << "unkCBuf" << YAML::Value;
                out << YAML::BeginSeq;
                for (uint32_t j = 0; j < 12; j++) {
                    out << YAML::Value << (uint32_t)std::get<uint8_t>(arguments.at(j + 3));
                }
                out << YAML::EndSeq;
                if (std::get<uint16_t>(arguments.at(0)) != 0) {
                    numChildren++;
                }
                break;
            case GeoLayoutOpCode::UnknownCmd10:
                out << YAML::Key << "UnknownCmd10";
                out << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "wrapMode" << YAML::Value << std::get<int32_t>(arguments.at(0));
                break;
            default:
                throw std::runtime_error("BK64::GeoLayoutModdingExporter: Unknown OpCode Found " +
                                         std::to_string(static_cast<uint32_t>(opCode)));
        }

        out << YAML::Key << "CMD_LEN" << YAML::Value << cmdLength;

        if (numChildren > 0) {
            childrenStack.emplace_back(0, numChildren, cmdLength);
            out << YAML::Key << "Children" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Child0" << YAML::Value << YAML::BeginSeq;
            continue;
        }

        out << YAML::EndMap;
        out << YAML::EndMap;

        while (cmdLength == 0 && !childrenStack.empty()) {
            auto& [childrenProcessed, totalChildren, parentCmdLength] = childrenStack.back();
            if (++childrenProcessed >= totalChildren) {
                out << YAML::EndSeq;
                out << YAML::EndMap;
                out << YAML::EndMap;
                out << YAML::EndMap;
                cmdLength = parentCmdLength;
                childrenStack.pop_back();
                // Exit Child
            } else {
                // Go To Next Child
                out << YAML::EndSeq;
                out << YAML::Key << ("Child" + std::to_string(childrenProcessed)) << YAML::Value << YAML::BeginSeq;
                break;
            }
        }
    }

    out << YAML::EndSeq;
    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> GeoLayoutFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto offset = GetSafeNode<uint32_t>(node, "offset");

    std::vector<GeoLayoutCommand> cmds;

    std::deque<uint32_t> offsetStack;

    offsetStack.push_back(0);

    while (true) {
        std::vector<GeoLayoutArg> args;
        auto localOffset = offsetStack.back();
        if (localOffset + 8 > segment.size) {
            break;
        }
        reader.Seek(localOffset, LUS::SeekOffsetType::Start);
        auto opCode = reader.ReadUInt32();
        auto cmdLength = reader.ReadUInt32();

        offsetStack.back() += cmdLength;

        if (cmdLength == 0) {
            offsetStack.pop_back();
        }

        switch (static_cast<GeoLayoutOpCode>(opCode)) {
            case GeoLayoutOpCode::UnknownCmd0: {
                auto childOffset = reader.ReadUInt16();
                auto shouldRotatePitch = reader.ReadUInt16();
                auto x = reader.ReadFloat();
                auto y = reader.ReadFloat();
                auto z = reader.ReadFloat();

                args.emplace_back(childOffset);
                args.emplace_back(shouldRotatePitch);
                args.emplace_back(x);
                args.emplace_back(y);
                args.emplace_back(z);
                if (childOffset != 0) {
                    offsetStack.push_back(localOffset + childOffset);
                }
                break;
            }
            case GeoLayoutOpCode::Sort: {
                auto x1 = reader.ReadFloat();
                auto y1 = reader.ReadFloat();
                auto z1 = reader.ReadFloat();
                auto x2 = reader.ReadFloat();
                auto y2 = reader.ReadFloat();
                auto z2 = reader.ReadFloat();

                reader.ReadUByte(); // pad
                auto layoutOrder = reader.ReadUByte();
                auto firstChildOffset = reader.ReadUInt16();
                reader.ReadUInt16(); // pad
                auto secondChildOffset = reader.ReadUInt16();

                args.emplace_back(x1);
                args.emplace_back(y1);
                args.emplace_back(z1);
                args.emplace_back(x2);
                args.emplace_back(y2);
                args.emplace_back(z2);
                args.emplace_back(layoutOrder);
                args.emplace_back(firstChildOffset);
                args.emplace_back(secondChildOffset);

                if (firstChildOffset != 0) {
                    offsetStack.push_back(localOffset + firstChildOffset);
                }
                if (secondChildOffset != 0) {
                    offsetStack.push_back(localOffset + secondChildOffset);
                }
                break;
            }
            case GeoLayoutOpCode::Bone: {
                auto childOffset = reader.ReadUByte();
                auto boneId = reader.ReadUByte();
                auto unkBoneInfo = reader.ReadUInt16();

                args.emplace_back(childOffset);
                args.emplace_back(boneId);
                args.emplace_back(unkBoneInfo);

                if (childOffset != 0) {
                    offsetStack.push_back(localOffset + childOffset);
                }
                break;
            }
            case GeoLayoutOpCode::LoadDL: {
                auto dlIndex = reader.ReadUInt16();
                auto triCount = reader.ReadUInt16();
                args.emplace_back(dlIndex);
                args.emplace_back(triCount);
                break;
            }
            case GeoLayoutOpCode::Skinning: {
                auto dlOffsetPreviousBone = reader.ReadUInt16();

                args.emplace_back(dlOffsetPreviousBone);

                while (true) {
                    auto dlOffset = reader.ReadUInt16();
                    if (dlOffset == 0) {
                        break;
                    }
                    args.emplace_back(dlOffset);
                }
                break;
            }
            case GeoLayoutOpCode::Branch: {
                auto cmdTargetOffset = reader.ReadUInt32();

                args.emplace_back(cmdTargetOffset);
                break;
            }
            case GeoLayoutOpCode::UnknownCmd7: {
                reader.ReadUInt16(); // pad
                auto dlIndex = reader.ReadUInt16();

                args.emplace_back(dlIndex);
                break;
            }
            case GeoLayoutOpCode::LOD: {
                auto maxDistance = reader.ReadFloat();
                auto minDistance = reader.ReadFloat();
                auto x = reader.ReadFloat();
                auto y = reader.ReadFloat();
                auto z = reader.ReadFloat();
                auto childOffset = reader.ReadUInt32();
                args.emplace_back(maxDistance);
                args.emplace_back(minDistance);
                args.emplace_back(x);
                args.emplace_back(y);
                args.emplace_back(z);
                args.emplace_back(childOffset);
                if (childOffset != 0) {
                    offsetStack.push_back(localOffset + childOffset);
                }
                break;
            }
            case GeoLayoutOpCode::ReferencePoint: {
                auto referencePointIndex = reader.ReadUInt16();
                auto boneIndex = reader.ReadUInt16();
                auto boneOffsetX = reader.ReadFloat();
                auto boneOffsetY = reader.ReadFloat();
                auto boneOffsetZ = reader.ReadFloat();

                args.emplace_back(referencePointIndex);
                args.emplace_back(boneIndex);
                args.emplace_back(boneOffsetX);
                args.emplace_back(boneOffsetY);
                args.emplace_back(boneOffsetZ);
                break;
            }
            case GeoLayoutOpCode::Selector: {
                auto childCount = reader.ReadUInt16();
                auto selectorIndex = reader.ReadUInt16();

                args.emplace_back(childCount);
                args.emplace_back(selectorIndex);

                for (uint16_t i = 0; i < childCount; i++) {
                    auto childOffset = reader.ReadUInt32();

                    args.emplace_back(childOffset);
                    if (childOffset != 0) {
                        offsetStack.push_back(localOffset + childOffset);
                    }
                }
                break;
            }
            case GeoLayoutOpCode::DrawDistance: {
                auto negX = reader.ReadInt16();
                auto negY = reader.ReadInt16();
                auto negZ = reader.ReadInt16();
                auto posX = reader.ReadInt16();
                auto posY = reader.ReadInt16();
                auto posZ = reader.ReadInt16();
                auto childOffset = reader.ReadInt16();
                auto unk16 = reader.ReadInt16();

                args.emplace_back(negX);
                args.emplace_back(negY);
                args.emplace_back(negZ);
                args.emplace_back(posX);
                args.emplace_back(posY);
                args.emplace_back(posZ);
                args.emplace_back(childOffset);
                args.emplace_back(unk16);
                // [port] unk14 is a child offset; the renderer follows it to recurse
                // into child geo commands
                if (childOffset != 0) {
                    offsetStack.push_back(localOffset + childOffset);
                }
                break;
            }
            case GeoLayoutOpCode::UnknownCmdE: {
                auto coords1X = reader.ReadInt16();
                auto coords1Y = reader.ReadInt16();
                auto coords1Z = reader.ReadInt16();
                auto unkE = reader.ReadInt16();
                auto childOffset = reader.ReadInt16();
                auto unk12 = reader.ReadInt16();

                args.emplace_back(coords1X);
                args.emplace_back(coords1Y);
                args.emplace_back(coords1Z);
                args.emplace_back(unkE);
                args.emplace_back(childOffset);
                args.emplace_back(unk12);
                // [port] unk10 is a child offset; the renderer follows it to recurse
                // into child geo commands
                if (childOffset != 0) {
                    offsetStack.push_back(localOffset + childOffset);
                }
                break;
            }
            case GeoLayoutOpCode::UnknownCmdF: {
                auto childOffset = reader.ReadUInt16();
                auto unkA = reader.ReadUByte();
                auto unkB = reader.ReadUByte();
                args.emplace_back(childOffset);
                args.emplace_back(unkA);
                args.emplace_back(unkB);
                for (int32_t i = 0; i < 12; i++) {
                    auto unkCBuf = reader.ReadUByte();
                    args.emplace_back(unkCBuf);
                }
                if (childOffset != 0) {
                    offsetStack.push_back(localOffset + childOffset);
                }
                break;
            }
            case GeoLayoutOpCode::UnknownCmd10: {
                auto wrapMode = reader.ReadInt32();
                args.emplace_back(wrapMode);
                break;
            }
            default:
                throw std::runtime_error("BK64::GeoLayoutFactory: Unknown OpCode Found " + std::to_string(opCode));
        }
        cmds.emplace_back(static_cast<GeoLayoutOpCode>(opCode), cmdLength, args, localOffset);

        if (offsetStack.size() == 0) {
            break;
        }
    }

    return std::make_shared<GeoLayoutData>(cmds);
}

} // namespace BK64
