#include "AnimationFactory.h"
#include "spdlog/spdlog.h"

void SM64::AnimationBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
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
}

std::optional<std::shared_ptr<IParsedData>> SM64::AnimationFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto headerNode = GetSafeNode<YAML::Node>(node, "header");
    auto valuesNode = GetSafeNode<YAML::Node>(node, "values");
    auto indexNode = GetSafeNode<YAML::Node>(node, "indices");

	LUS::BinaryReader header(reinterpret_cast<char *>(buffer.data()) + GetSafeNode<uint32_t>(headerNode, "offset"), GetSafeNode<uint32_t>(headerNode, "size"));
	header.SetEndianness(LUS::Endianness::Big);

	auto flags = header.ReadInt16();
	auto animYTransDivisor = header.ReadInt16();
	auto startFrame = header.ReadInt16();
	auto loopStart = header.ReadInt16();
	auto loopEnd = header.ReadInt16();
	auto unusedBoneCount = header.ReadInt16();
    header.ReadUInt32();
    header.ReadUInt32();
	auto length = header.ReadUInt32();

    LUS::BinaryReader indices(reinterpret_cast<char*>(buffer.data()) + GetSafeNode<uint32_t>(indexNode, "offset"), GetSafeNode<uint32_t>(indexNode, "size"));
    indices.SetEndianness(LUS::Endianness::Big);
    size_t indexLength = GetSafeNode<uint32_t>(indexNode, "size") / sizeof(int16_t);
    std::vector<int16_t> indicesData;
    for (size_t i = 0; i < indexLength; i++) {
        indicesData.push_back(indices.ReadInt16());
    }

    LUS::BinaryReader values(reinterpret_cast<char*>(buffer.data()) + GetSafeNode<uint32_t>(valuesNode, "offset"), GetSafeNode<uint32_t>(valuesNode, "size"));
	values.SetEndianness(LUS::Endianness::Big);
	size_t entries = GetSafeNode<uint32_t>(valuesNode, "size") / sizeof(uint16_t);
    std::vector<uint16_t> valuesData;
    for (size_t i = 0; i < entries; i++) {
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