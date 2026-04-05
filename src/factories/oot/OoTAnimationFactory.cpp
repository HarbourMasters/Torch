#ifdef OOT_SUPPORT

#include "OoTAnimationFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

std::optional<std::shared_ptr<IParsedData>> OoTAnimationFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // Check for legacy animation type (data stays in ROM segment, not extracted)
    auto animType = GetSafeNode<std::string>(node, "anim_type", "normal");
    if (animType == "legacy") {
        auto anim = std::make_shared<OoTNormalAnimationData>();
        anim->frameCount = 0;
        anim->limit = 0;
        anim->isLegacy = true;
        return anim;
    }

    // ROM layout: AnimationHeader (16 bytes)
    // +0x00: int16 frameCount
    // +0x02: int16 padding
    // +0x04: segptr rotationValues
    // +0x08: segptr rotationIndices
    // +0x0C: int16 limit
    // +0x0E: int16 padding
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0x10);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto anim = std::make_shared<OoTNormalAnimationData>();
    anim->frameCount = reader.ReadInt16();
    reader.ReadInt16(); // padding
    uint32_t rawRotValues = reader.ReadUInt32();
    uint32_t rawRotIndices = reader.ReadUInt32();
    SPDLOG_WARN("Animation raw pointers: rotValues=0x{:08X} rotIndices=0x{:08X}", rawRotValues, rawRotIndices);
    // Log segment map
    for (auto& [seg, off] : Companion::Instance->GetConfig().segment.local) {
        SPDLOG_WARN("  segment[{}] = 0x{:X}", seg, off);
    }
    uint32_t rotValuesAddr = Companion::Instance->PatchVirtualAddr(rawRotValues);
    uint32_t rotIndicesAddr = Companion::Instance->PatchVirtualAddr(rawRotIndices);
    SPDLOG_WARN("Animation patched: rotValues=0x{:08X} rotIndices=0x{:08X}", rotValuesAddr, rotIndicesAddr);
    anim->limit = reader.ReadInt16();

    // Translate segmented addresses to file offsets
    uint32_t rotValuesOffset = Decompressor::TranslateAddr(rotValuesAddr);
    uint32_t rotIndicesOffset = Decompressor::TranslateAddr(rotIndicesAddr);
    uint32_t animHeaderOffset = Decompressor::TranslateAddr(Companion::Instance->PatchVirtualAddr(GetSafeNode<uint32_t>(node, "offset")));

    // Read rotation values: array of uint16 from rotValues to rotIndices
    uint32_t rotValuesCount = (rotIndicesOffset - rotValuesOffset) / 2;
    if (rotValuesCount > 0 && rotValuesOffset < rotIndicesOffset) {
        YAML::Node rvNode;
        rvNode["offset"] = rotValuesAddr;
        auto rvRaw = Decompressor::AutoDecode(rvNode, buffer, rotValuesCount * 2);
        LUS::BinaryReader rvReader(rvRaw.segment.data, rvRaw.segment.size);
        rvReader.SetEndianness(Torch::Endianness::Big);

        for (uint32_t i = 0; i < rotValuesCount; i++) {
            anim->rotationValues.push_back(rvReader.ReadUInt16());
        }
    }

    // Read rotation indices: array of {x,y,z} uint16 from rotIndices to animHeader
    uint32_t rotIndicesCount = (animHeaderOffset - rotIndicesOffset) / 6;
    if (rotIndicesCount > 0 && rotIndicesOffset < animHeaderOffset) {
        YAML::Node riNode;
        riNode["offset"] = rotIndicesAddr;
        auto riRaw = Decompressor::AutoDecode(riNode, buffer, rotIndicesCount * 6);
        LUS::BinaryReader riReader(riRaw.segment.data, riRaw.segment.size);
        riReader.SetEndianness(Torch::Endianness::Big);

        for (uint32_t i = 0; i < rotIndicesCount; i++) {
            RotationIndex ri;
            ri.x = riReader.ReadUInt16();
            ri.y = riReader.ReadUInt16();
            ri.z = riReader.ReadUInt16();
            anim->rotationIndices.push_back(ri);
        }
    }

    return anim;
}

ExportResult OoTAnimationBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                 std::string& entryName, YAML::Node& node,
                                                 std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto anim = std::static_pointer_cast<OoTNormalAnimationData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTAnimation, 0);

    if (anim->isLegacy) {
        writer.Write(static_cast<uint32_t>(OoTAnimationType::Legacy));
        writer.Finish(write);
        return std::nullopt;
    }

    writer.Write(static_cast<uint32_t>(OoTAnimationType::Normal));
    writer.Write(anim->frameCount);

    writer.Write(static_cast<uint32_t>(anim->rotationValues.size()));
    for (auto& val : anim->rotationValues) {
        writer.Write(val);
    }

    writer.Write(static_cast<uint32_t>(anim->rotationIndices.size()));
    for (auto& ri : anim->rotationIndices) {
        writer.Write(ri.x);
        writer.Write(ri.y);
        writer.Write(ri.z);
    }

    writer.Write(anim->limit);

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
