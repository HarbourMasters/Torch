#include "AnimationFactory.h"

#include <utils/Decompressor.h>
#include "spdlog/spdlog.h"

#define ANIM_INDEX_COUNT(boneCount) ((boneCount) + 1) * 2 * 6

ExportResult SM64::AnimationBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    const auto anim = std::static_pointer_cast<AnimationData>(raw);

    WriteHeader(writer, LUS::ResourceType::Anim, 0);
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
    auto header = Decompressor::AutoDecode(node, buffer).GetReader();
	header.SetEndianness(LUS::Endianness::Big);

	auto flags = header.ReadInt16();
	auto animYTransDivisor = header.ReadInt16();
	auto startFrame = header.ReadInt16();
	auto loopStart = header.ReadInt16();
	auto loopEnd = header.ReadInt16();
	auto unusedBoneCount = header.ReadInt16();
    auto valuesAddr = header.ReadUInt32();
    auto indicesAddr = header.ReadUInt32();
	auto length = header.ReadUInt32();

    auto values = Decompressor::AutoDecodeByOffset(valuesAddr, indicesAddr - valuesAddr, buffer).GetReader();
    values.SetEndianness(LUS::Endianness::Big);

    std::vector<uint16_t> valuesData;
    for (size_t i = 0; i < values.GetLength() / sizeof(uint16_t); i++) {
        valuesData.push_back(values.ReadUInt16());
    }

    auto indicesCount = ANIM_INDEX_COUNT(unusedBoneCount);
    auto indices = Decompressor::AutoDecodeByOffset(indicesAddr, indicesCount * sizeof(int16_t), buffer).GetReader();
    indices.SetEndianness(LUS::Endianness::Big);

    std::vector<int16_t> indicesData;
    for (size_t i = 0; i < indicesCount; i++) {
        indicesData.push_back(indices.ReadInt16());
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