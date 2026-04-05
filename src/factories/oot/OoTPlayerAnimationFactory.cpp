#ifdef OOT_SUPPORT

#include "OoTPlayerAnimationFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

// ==================== OoT Player Animation Header Factory ====================

std::optional<std::shared_ptr<IParsedData>> OoTPlayerAnimationHeaderFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // ROM layout: PlayerAnimationHeader (8 bytes)
    // +0x00: int16 frameCount
    // +0x02: int16 padding
    // +0x04: uint32 segPtr (segment 7 pointer to link_animetion data)
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0x08);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto anim = std::make_shared<OoTPlayerAnimationHeaderData>();
    anim->frameCount = reader.ReadInt16();
    reader.ReadInt16(); // padding
    uint32_t segPtr = reader.ReadUInt32();

    // Resolve the segment pointer to a path in link_animetion
    auto path = Companion::Instance->GetStringByAddr(segPtr);
    if (path.has_value()) {
        anim->animDataPath = path.value();
    } else {
        SPDLOG_WARN("PlayerAnimation: Could not resolve segment pointer 0x{:08X}", segPtr);
        anim->animDataPath = "";
    }

    return anim;
}

ExportResult OoTPlayerAnimationHeaderBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                             std::string& entryName, YAML::Node& node,
                                                             std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto anim = std::static_pointer_cast<OoTPlayerAnimationHeaderData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTAnimation, 0);

    writer.Write(static_cast<uint32_t>(OoTAnimationType::Link));
    writer.Write(anim->frameCount);
    writer.Write("__OTR__" + anim->animDataPath);

    writer.Finish(write);
    return std::nullopt;
}

// ==================== OoT Player Animation Data Factory ====================

std::optional<std::shared_ptr<IParsedData>> OoTPlayerAnimationDataFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // Player animation data is a flat array of int16 limb rotation values.
    // Size = (6 * 22 + 2) * frameCount bytes = 134 bytes per frame.
    int32_t frameCount = GetSafeNode<int32_t>(node, "frame_count");
    uint32_t dataSize = (6 * 22 + 2) * frameCount;

    auto [_, segment] = Decompressor::AutoDecode(node, buffer, dataSize);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto anim = std::make_shared<OoTPlayerAnimationData>();
    uint32_t numEntries = dataSize / 2;
    anim->limbRotData.reserve(numEntries);
    for (uint32_t i = 0; i < numEntries; i++) {
        anim->limbRotData.push_back(reader.ReadInt16());
    }

    return anim;
}

ExportResult OoTPlayerAnimationDataBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                           std::string& entryName, YAML::Node& node,
                                                           std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto anim = std::static_pointer_cast<OoTPlayerAnimationData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTPlayerAnimation, 0);

    writer.Write(static_cast<uint32_t>(anim->limbRotData.size()));
    for (auto& val : anim->limbRotData) {
        writer.Write(val);
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
