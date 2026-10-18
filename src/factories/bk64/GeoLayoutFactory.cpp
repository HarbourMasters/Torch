#include "GeoLayoutFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <type_traits>

#include "BKEmitText.h"

namespace BK64 {

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
        case GeoLayoutOpCode::NOP:
        case GeoLayoutOpCode::NOP2:
        case GeoLayoutOpCode::NOP3:
            bodySize = 4;
            break; // 4 bytes Banjo's Backpack writes and nothing reads
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
            bodySize = static_cast<uint32_t>(cmd.args.size()) + 1;
            break; // 2+1+1, then one byte per id
        case GeoLayoutOpCode::UnknownCmd10:
            bodySize = 4;
            break; // 4
        default:
            break;
    }
    return 8 + bodySize; // 8 = opcode(4) + cmdLength(4)
}

ExportResult GeoLayoutCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto geo = std::static_pointer_cast<GeoLayoutData>(raw);

    return offset;
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
            case GeoLayoutOpCode::NOP:
            case GeoLayoutOpCode::NOP2:
            case GeoLayoutOpCode::NOP3:
                for (size_t i = 0; i < arguments.size(); i++)
                    writeU8(std::get<uint8_t>(arguments[i]));
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

static const std::map<GeoLayoutOpCode, const char*> sGeoCommandNames = {
    { GeoLayoutOpCode::UnknownCmd0, "UnknownCmd0" },
    { GeoLayoutOpCode::Sort, "Sort" },
    { GeoLayoutOpCode::Bone, "Bone" },
    { GeoLayoutOpCode::LoadDL, "LoadDL" },
    { GeoLayoutOpCode::NOP, "NOP" },
    { GeoLayoutOpCode::Skinning, "Skinning" },
    { GeoLayoutOpCode::Branch, "Branch" },
    { GeoLayoutOpCode::UnknownCmd7, "UnknownCmd7" },
    { GeoLayoutOpCode::LOD, "LOD" },
    { GeoLayoutOpCode::NOP2, "NOP2" },
    { GeoLayoutOpCode::ReferencePoint, "ReferencePoint" },
    { GeoLayoutOpCode::NOP3, "NOP3" },
    { GeoLayoutOpCode::Selector, "Selector" },
    { GeoLayoutOpCode::DrawDistance, "DrawDistance" },
    { GeoLayoutOpCode::UnknownCmdE, "UnknownCmdE" },
    { GeoLayoutOpCode::UnknownCmdF, "UnknownCmdF" },
    { GeoLayoutOpCode::UnknownCmd10, "UnknownCmd10" },
};

// Which arguments hold a child's offset from the command. The yaml nests the children instead.
static std::vector<size_t> ChildArgs(const GeoLayoutCommand& cmd) {
    switch (cmd.opCode) {
        case GeoLayoutOpCode::UnknownCmd0:
        case GeoLayoutOpCode::Bone:
        case GeoLayoutOpCode::UnknownCmdF:
            return { 0 };
        case GeoLayoutOpCode::Sort:
            return { 7, 8 };
        case GeoLayoutOpCode::LOD:
            return { 5 };
        case GeoLayoutOpCode::DrawDistance:
            return { 6 };
        case GeoLayoutOpCode::UnknownCmdE:
            return { 4 };
        case GeoLayoutOpCode::Selector: {
            std::vector<size_t> args;
            for (size_t i = 2; i < cmd.args.size(); i++) {
                args.push_back(i);
            }
            return args;
        }
        default:
            return {};
    }
}

static uint32_t ArgNumber(const GeoLayoutArg& arg) {
    return std::visit([](auto value) { return (uint32_t)(int32_t)value; }, arg);
}

static void SetChildArg(GeoLayoutArg& arg, uint32_t offset) {
    std::visit([offset](auto& value) { value = (std::decay_t<decltype(value)>)offset; }, arg);
}

static uint32_t MostChildOffset(const GeoLayoutArg& arg) {
    return std::visit(
        [](auto value) {
            using T = decltype(value);
            return std::is_same_v<T, float> ? 0u : (uint32_t)std::numeric_limits<T>::max();
        },
        arg);
}

// Every retail geo layout pads its commands to 8 bytes, with 8 more behind a skinning list.
static uint32_t GeoLayoutSpace(const GeoLayoutCommand& cmd) {
    const uint32_t size = (GetGeoCommandByteSize(cmd) + 7) & ~7u;
    return cmd.opCode == GeoLayoutOpCode::Skinning ? size + 8 : size;
}

static void EmitGeoChain(YAML::Emitter& out, const std::map<uint32_t, const GeoLayoutCommand*>& at, uint32_t offset,
                         int depth);

static void EmitGeoCommand(YAML::Emitter& out, const std::map<uint32_t, const GeoLayoutCommand*>& at,
                           const GeoLayoutCommand& cmd, int depth) {
    const auto& arguments = cmd.args;
    out << YAML::BeginMap;
    out << YAML::Key << sGeoCommandNames.at(cmd.opCode);
    out << YAML::Value << YAML::BeginMap;
    switch (cmd.opCode) {
        case GeoLayoutOpCode::UnknownCmd0:
            out << YAML::Key << "shouldRotatePitch" << YAML::Value << (bool)std::get<uint16_t>(arguments.at(1));
            out << YAML::Key << "x" << YAML::Value << std::get<float>(arguments.at(2));
            out << YAML::Key << "y" << YAML::Value << std::get<float>(arguments.at(3));
            out << YAML::Key << "z" << YAML::Value << std::get<float>(arguments.at(4));
            break;
        case GeoLayoutOpCode::Sort:
            out << YAML::Key << "x1" << YAML::Value << std::get<float>(arguments.at(0));
            out << YAML::Key << "y1" << YAML::Value << std::get<float>(arguments.at(1));
            out << YAML::Key << "z1" << YAML::Value << std::get<float>(arguments.at(2));
            out << YAML::Key << "x2" << YAML::Value << std::get<float>(arguments.at(3));
            out << YAML::Key << "y2" << YAML::Value << std::get<float>(arguments.at(4));
            out << YAML::Key << "z2" << YAML::Value << std::get<float>(arguments.at(5));
            out << YAML::Key << "layoutOrder" << YAML::Value << (uint32_t)std::get<uint8_t>(arguments.at(6));
            break;
        case GeoLayoutOpCode::Bone:
            out << YAML::Key << "boneId" << YAML::Value << (uint32_t)std::get<uint8_t>(arguments.at(1));
            out << YAML::Key << "unkBoneInfo" << YAML::Value << std::get<uint16_t>(arguments.at(2));
            break;
        case GeoLayoutOpCode::LoadDL:
            out << YAML::Key << "dlIndex" << YAML::Value << std::get<uint16_t>(arguments.at(0));
            out << YAML::Key << "triCount" << YAML::Value << std::get<uint16_t>(arguments.at(1));
            break;
        // Nothing reads the body, so there is nothing worth writing out
        case GeoLayoutOpCode::NOP:
        case GeoLayoutOpCode::NOP2:
        case GeoLayoutOpCode::NOP3:
            break;
        case GeoLayoutOpCode::Skinning:
            out << YAML::Key << "dlOffsetPreviousBone" << YAML::Value << std::get<uint16_t>(arguments.at(0));
            out << YAML::Key << "dlOffsets" << YAML::Value << YAML::Flow << YAML::BeginSeq;
            for (size_t j = 1; j < arguments.size(); j++) {
                out << std::get<uint16_t>(arguments.at(j));
            }
            out << YAML::EndSeq;
            break;
        case GeoLayoutOpCode::Branch:
            out << YAML::Key << "cmdTargetOffset" << YAML::Value << std::get<uint32_t>(arguments.at(0));
            break;
        case GeoLayoutOpCode::UnknownCmd7:
            out << YAML::Key << "dlIndex" << YAML::Value << std::get<uint16_t>(arguments.at(0));
            break;
        case GeoLayoutOpCode::LOD:
            out << YAML::Key << "maxDistance" << YAML::Value << std::get<float>(arguments.at(0));
            out << YAML::Key << "minDistance" << YAML::Value << std::get<float>(arguments.at(1));
            out << YAML::Key << "x" << YAML::Value << std::get<float>(arguments.at(2));
            out << YAML::Key << "y" << YAML::Value << std::get<float>(arguments.at(3));
            out << YAML::Key << "z" << YAML::Value << std::get<float>(arguments.at(4));
            break;
        case GeoLayoutOpCode::ReferencePoint:
            out << YAML::Key << "referencePointIndex" << YAML::Value << std::get<uint16_t>(arguments.at(0));
            out << YAML::Key << "boneIndex" << YAML::Value << std::get<uint16_t>(arguments.at(1));
            out << YAML::Key << "boneOffsetX" << YAML::Value << std::get<float>(arguments.at(2));
            out << YAML::Key << "boneOffsetY" << YAML::Value << std::get<float>(arguments.at(3));
            out << YAML::Key << "boneOffsetZ" << YAML::Value << std::get<float>(arguments.at(4));
            break;
        case GeoLayoutOpCode::Selector:
            out << YAML::Key << "selectorIndex" << YAML::Value << std::get<uint16_t>(arguments.at(1));
            break;
        case GeoLayoutOpCode::DrawDistance:
            out << YAML::Key << "negX" << YAML::Value << std::get<int16_t>(arguments.at(0));
            out << YAML::Key << "negY" << YAML::Value << std::get<int16_t>(arguments.at(1));
            out << YAML::Key << "negZ" << YAML::Value << std::get<int16_t>(arguments.at(2));
            out << YAML::Key << "posX" << YAML::Value << std::get<int16_t>(arguments.at(3));
            out << YAML::Key << "posY" << YAML::Value << std::get<int16_t>(arguments.at(4));
            out << YAML::Key << "posZ" << YAML::Value << std::get<int16_t>(arguments.at(5));
            out << YAML::Key << "unk16" << YAML::Value << std::get<int16_t>(arguments.at(7));
            break;
        case GeoLayoutOpCode::UnknownCmdE:
            out << YAML::Key << "x" << YAML::Value << std::get<int16_t>(arguments.at(0));
            out << YAML::Key << "y" << YAML::Value << std::get<int16_t>(arguments.at(1));
            out << YAML::Key << "z" << YAML::Value << std::get<int16_t>(arguments.at(2));
            out << YAML::Key << "distance" << YAML::Value << std::get<int16_t>(arguments.at(3));
            out << YAML::Key << "animMtxId" << YAML::Value << std::get<int16_t>(arguments.at(5));
            break;
        case GeoLayoutOpCode::UnknownCmdF:
            out << YAML::Key << "flags" << YAML::Value << (uint32_t)std::get<uint8_t>(arguments.at(2));
            out << YAML::Key << "ids" << YAML::Value << YAML::Flow << YAML::BeginSeq;
            for (size_t j = 3; j < arguments.size(); j++) {
                out << (uint32_t)std::get<uint8_t>(arguments.at(j));
            }
            out << YAML::EndSeq;
            break;
        case GeoLayoutOpCode::UnknownCmd10:
            out << YAML::Key << "wrapMode" << YAML::Value << std::get<int32_t>(arguments.at(0));
            break;
        default:
            throw std::runtime_error("BK64::GeoLayoutModdingExporter: Unknown OpCode Found " +
                                     std::to_string(static_cast<uint32_t>(cmd.opCode)));
    }

    // A selector lists every option, empty ones too; elsewhere an empty child is left out
    const auto children = ChildArgs(cmd);
    const bool any =
        std::any_of(children.begin(), children.end(), [&](size_t arg) { return ArgNumber(arguments.at(arg)) != 0; });
    if (children.size() == 1 && any && cmd.opCode != GeoLayoutOpCode::Selector) {
        out << YAML::Key << "Child" << YAML::Value;
        EmitGeoChain(out, at, cmd.originalOffset + ArgNumber(arguments.at(children[0])), depth + 1);
    } else if (any || cmd.opCode == GeoLayoutOpCode::Selector) {
        out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
        for (const size_t arg : children) {
            const uint32_t offset = ArgNumber(arguments.at(arg));
            if (offset == 0) {
                out << YAML::Flow << YAML::BeginSeq << YAML::EndSeq;
            } else {
                EmitGeoChain(out, at, cmd.originalOffset + offset, depth + 1);
            }
        }
        out << YAML::EndSeq;
    }
    out << YAML::EndMap;
    out << YAML::EndMap;
}

static void EmitGeoChain(YAML::Emitter& out, const std::map<uint32_t, const GeoLayoutCommand*>& at, uint32_t offset,
                         int depth) {
    out << YAML::BeginSeq;
    for (size_t steps = 0; depth < 64 && steps < at.size(); steps++) {
        const auto found = at.find(offset);
        if (found == at.end()) {
            SPDLOG_ERROR("[BK64:GEO] No command at 0x{:X}", offset);
            break;
        }
        EmitGeoCommand(out, at, *found->second, depth);
        if (found->second->cmdLength == 0) {
            break;
        }
        offset += found->second->cmdLength;
    }
    out << YAML::EndSeq;
}

ExportResult GeoLayoutModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto geo = std::static_pointer_cast<GeoLayoutData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    std::map<uint32_t, const GeoLayoutCommand*> at;
    for (const auto& cmd : geo->mCmds) {
        at[cmd.originalOffset] = &cmd;
    }

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);
    EmitGeoChain(out, at, 0, 0);
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
            case GeoLayoutOpCode::NOP:
            case GeoLayoutOpCode::NOP2:
            case GeoLayoutOpCode::NOP3: {
                // sGeoCmdList dispatches these to modelRender_geoCmd_NOP--nothing reads the body.
                // Keep the bytes so the binary exporter writes the command back as it was.
                for (int32_t i = 0; i < 4; i++) {
                    args.emplace_back(reader.ReadUByte());
                }
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
                auto count = reader.ReadUByte();
                auto flags = reader.ReadUByte();
                args.emplace_back(childOffset);
                args.emplace_back(count);
                args.emplace_back(flags);
                for (int32_t i = 0; i < count; i++) {
                    args.emplace_back(reader.ReadUByte());
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

// The command with its child offsets left at 0, for the layout to fill in.
static GeoLayoutCommand ReadGeoCommand(const YAML::Node& entry, const std::string& where) {
    if (!entry.IsMap() || entry.size() != 1) {
        YamlFail(where, "needs one command");
    }
    const std::string name = entry.begin()->first.as<std::string>();
    const YAML::Node fields = entry.begin()->second;
    const auto found = std::find_if(sGeoCommandNames.begin(), sGeoCommandNames.end(),
                                    [&](const auto& command) { return name == command.second; });
    if (found == sGeoCommandNames.end()) {
        YamlFail(where, name + " isn't a geo layout command");
    }
    const std::string at = where + " " + name;
    std::vector<GeoLayoutArg> args;
    switch (found->first) {
        case GeoLayoutOpCode::UnknownCmd0: {
            bool rotate = false;
            if (!fields["shouldRotatePitch"] || !YAML::convert<bool>::decode(fields["shouldRotatePitch"], rotate)) {
                YamlFail(at, "needs shouldRotatePitch, true or false");
            }
            args = { (uint16_t)0, (uint16_t)rotate, YamlFloat(fields, "x", at), YamlFloat(fields, "y", at),
                     YamlFloat(fields, "z", at) };
            break;
        }
        case GeoLayoutOpCode::Sort:
            args = { YamlFloat(fields, "x1", at),
                     YamlFloat(fields, "y1", at),
                     YamlFloat(fields, "z1", at),
                     YamlFloat(fields, "x2", at),
                     YamlFloat(fields, "y2", at),
                     YamlFloat(fields, "z2", at),
                     (uint8_t)YamlNumber(fields, "layoutOrder", at, UINT8_MAX),
                     (uint16_t)0,
                     (uint16_t)0 };
            break;
        case GeoLayoutOpCode::Bone:
            args = { (uint8_t)0, (uint8_t)YamlNumber(fields, "boneId", at, UINT8_MAX),
                     (uint16_t)YamlNumber(fields, "unkBoneInfo", at, UINT16_MAX) };
            break;
        case GeoLayoutOpCode::LoadDL:
            args = { (uint16_t)YamlNumber(fields, "dlIndex", at, UINT16_MAX),
                     (uint16_t)YamlNumber(fields, "triCount", at, UINT16_MAX) };
            break;
        case GeoLayoutOpCode::NOP:
        case GeoLayoutOpCode::NOP2:
        case GeoLayoutOpCode::NOP3:
            args = { (uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)0 };
            break;
        case GeoLayoutOpCode::Skinning: {
            args = { (uint16_t)YamlNumber(fields, "dlOffsetPreviousBone", at, UINT16_MAX) };
            if (!fields["dlOffsets"] || !fields["dlOffsets"].IsSequence()) {
                YamlFail(at, "needs a dlOffsets list");
            }
            for (const auto& offset : fields["dlOffsets"]) {
                const auto value = (uint16_t)YamlNumber(offset, at + " dlOffsets", UINT16_MAX);
                // A 0 ends the list
                if (value == 0) {
                    YamlFail(at + " dlOffsets", "can't hold a 0");
                }
                args.emplace_back(value);
            }
            break;
        }
        case GeoLayoutOpCode::Branch:
            args = { YamlNumber(fields, "cmdTargetOffset", at, UINT32_MAX) };
            break;
        case GeoLayoutOpCode::UnknownCmd7:
            args = { (uint16_t)YamlNumber(fields, "dlIndex", at, UINT16_MAX) };
            break;
        case GeoLayoutOpCode::LOD:
            args = { YamlFloat(fields, "maxDistance", at), YamlFloat(fields, "minDistance", at),
                     YamlFloat(fields, "x", at),           YamlFloat(fields, "y", at),
                     YamlFloat(fields, "z", at),           (uint32_t)0 };
            break;
        case GeoLayoutOpCode::ReferencePoint:
            args = { (uint16_t)YamlNumber(fields, "referencePointIndex", at, UINT16_MAX),
                     (uint16_t)YamlNumber(fields, "boneIndex", at, UINT16_MAX), YamlFloat(fields, "boneOffsetX", at),
                     YamlFloat(fields, "boneOffsetY", at), YamlFloat(fields, "boneOffsetZ", at) };
            break;
        case GeoLayoutOpCode::Selector: {
            const size_t options = fields["Children"] ? fields["Children"].size() : 0;
            if (options > UINT16_MAX) {
                YamlFail(at, "has more than 65535 options");
            }
            args = { (uint16_t)options, (uint16_t)YamlNumber(fields, "selectorIndex", at, UINT16_MAX) };
            args.insert(args.end(), options, (uint32_t)0);
            break;
        }
        case GeoLayoutOpCode::DrawDistance:
            args = { YamlShort(fields, "negX", at),
                     YamlShort(fields, "negY", at),
                     YamlShort(fields, "negZ", at),
                     YamlShort(fields, "posX", at),
                     YamlShort(fields, "posY", at),
                     YamlShort(fields, "posZ", at),
                     (int16_t)0,
                     YamlShort(fields, "unk16", at) };
            break;
        case GeoLayoutOpCode::UnknownCmdE:
            args = { YamlShort(fields, "x", at),
                     YamlShort(fields, "y", at),
                     YamlShort(fields, "z", at),
                     YamlShort(fields, "distance", at),
                     (int16_t)0,
                     YamlShort(fields, "animMtxId", at) };
            break;
        case GeoLayoutOpCode::UnknownCmdF: {
            const YAML::Node ids = fields["ids"];
            if (!ids || !ids.IsSequence() || ids.size() > UINT8_MAX) {
                YamlFail(at, "needs an ids list of up to 255");
            }
            args = { (uint16_t)0, (uint8_t)ids.size(), (uint8_t)YamlNumber(fields, "flags", at, UINT8_MAX) };
            for (const auto& id : ids) {
                args.emplace_back((uint8_t)YamlNumber(id, at + " ids", UINT8_MAX));
            }
            break;
        }
        case GeoLayoutOpCode::UnknownCmd10: {
            if (!fields["wrapMode"]) {
                YamlFail(at, "needs wrapMode");
            }
            int32_t wrapMode;
            try {
                wrapMode = fields["wrapMode"].as<int32_t>();
            } catch (const YAML::Exception&) {
                YamlFail(at + " wrapMode", "\"" + fields["wrapMode"].as<std::string>() + "\" isn't a number");
            }
            args = { wrapMode };
            break;
        }
    }
    return GeoLayoutCommand(found->first, 0, args);
}

// Lays a chain out from start, each command's children right behind it, and returns where it ends.
static uint32_t PlaceGeoChain(const YAML::Node& chain, uint32_t start, std::vector<GeoLayoutCommand>& cmds,
                              const std::string& where, int depth) {
    if (depth > 64) {
        YamlFail(where, "nests deeper than 64");
    }
    if (!chain.IsSequence()) {
        YamlFail(where, "needs a list of commands");
    }
    uint32_t pos = start;
    for (size_t i = 0; i < chain.size(); i++) {
        const std::string at = where + "[" + std::to_string(i) + "]";
        GeoLayoutCommand cmd = ReadGeoCommand(chain[i], at);
        const auto slots = ChildArgs(cmd);
        const YAML::Node fields = chain[i].begin()->second;
        std::vector<YAML::Node> children;
        if (slots.size() == 1 && cmd.opCode != GeoLayoutOpCode::Selector && fields["Child"]) {
            children.push_back(fields["Child"]);
        } else if (fields["Children"]) {
            if (!fields["Children"].IsSequence() || fields["Children"].size() != slots.size()) {
                YamlFail(at, "needs " + std::to_string(slots.size()) + " lists under Children");
            }
            for (const auto& child : fields["Children"]) {
                children.push_back(static_cast<const YAML::Node&>(child));
            }
        }
        cmd.originalOffset = pos;
        uint32_t end = pos + GeoLayoutSpace(cmd);
        const size_t index = cmds.size();
        cmds.push_back(cmd);
        for (size_t k = 0; k < children.size(); k++) {
            if (children[k].IsSequence() && children[k].size() == 0) {
                continue;
            }
            auto& arg = cmds[index].args[slots[k]];
            if (end - pos > MostChildOffset(arg)) {
                YamlFail(at, "has too much before its last child for the offset to reach it");
            }
            SetChildArg(arg, end - pos);
            const std::string child = slots.size() == 1 ? " Child" : " Children[" + std::to_string(k) + "]";
            end = PlaceGeoChain(children[k], end, cmds, at + child, depth + 1);
        }
        cmds[index].cmdLength = i + 1 == chain.size() ? 0 : end - pos;
        pos = end;
    }
    return pos;
}

std::optional<std::shared_ptr<IParsedData>> GeoLayoutFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                            YAML::Node& node) {
    std::vector<GeoLayoutCommand> cmds;
    PlaceGeoChain(YamlEntry(buffer, GetSafeNode<std::string>(node, "symbol")), 0, cmds, "", 0);
    return std::make_shared<GeoLayoutData>(cmds);
}

} // namespace BK64
