#include "AnimationFactory.h"
#include "spdlog/spdlog.h"

#include "utils/Decompressor.h"

#define ANIMINDEX_COUNT(boneCount) (sizeof(boneCount) + 1) * 6 * sizeof(uint16_t)

ExportResult SM64::AnimationBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    const auto anim = std::static_pointer_cast<AnimationData>(raw);

    WriteHeader(writer, Torch::ResourceType::Anim, 0);
    writer.Write(anim->mFlags);
    writer.Write(anim->mAnimYTransDivisor);
    writer.Write(anim->mStartFrame);
    writer.Write(anim->mLoopStart);
    writer.Write(anim->mLoopEnd);
    writer.Write(anim->mUnusedBoneCount);
    writer.Write(static_cast<uint64_t>(anim->mLength));

    writer.Write(static_cast<uint32_t>(anim->mIndices.size()));
    for (const auto& index : anim->mIndices) {
        writer.Write(index);
    }

    writer.Write(static_cast<uint32_t>(anim->mEntries.size()));
    for (const auto& entry : anim->mEntries) {
        writer.Write(entry);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SM64::AnimationFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto offset = node["offset"];

    auto [_, data] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader header = LUS::BinaryReader(data.data, data.size);
    header.SetEndianness(Torch::Endianness::Big);

    auto flags = header.ReadInt16();
    auto animYTransDivisor = header.ReadInt16();
    auto startFrame = header.ReadInt16();
    auto loopStart = header.ReadInt16();
    auto loopEnd = header.ReadInt16();
    auto unusedBoneCount = header.ReadInt16();
    auto valuesAddr = header.ReadUInt32();
    auto indexAddr = header.ReadUInt32();
    auto length = header.ReadUInt32();

    size_t indexLength = ANIMINDEX_COUNT(unusedBoneCount);

    LUS::BinaryReader indices = LUS::BinaryReader(data.data + indexAddr, indexLength * sizeof(uint16_t));
    indices.SetEndianness(Torch::Endianness::Big);
    std::vector<int16_t> indicesData;
    for (size_t i = 0; i < indexLength; i++) {
        indicesData.push_back(indices.ReadInt16());
    }

    size_t valuesSize = length * sizeof(uint16_t);
    LUS::BinaryReader values = LUS::BinaryReader(data.data + valuesAddr, valuesSize);
    values.SetEndianness(Torch::Endianness::Big);
    std::vector<uint16_t> valuesData;
    for (size_t i = 0; i < valuesSize / sizeof(uint16_t); i++) {
        valuesData.push_back(values.ReadUInt16());
    }

    SPDLOG_INFO("Flags: {}", flags);
    SPDLOG_INFO("Anim-Y Trans Divisor: {}", animYTransDivisor);
    SPDLOG_INFO("Start Frame: {}", startFrame);
    SPDLOG_INFO("Loop Start: {}", loopStart);
    SPDLOG_INFO("Loop End: {}", loopEnd);
    SPDLOG_INFO("Unused Bone Count: {}", unusedBoneCount);
    SPDLOG_INFO("Length: {}", length);

    return std::make_shared<AnimationData>(flags, animYTransDivisor, startFrame, loopStart, loopEnd, unusedBoneCount, length, indicesData, valuesData);
}