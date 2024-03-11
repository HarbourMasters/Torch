#include "AnimFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define HEX(x) std::hex << std::setfill(' ') << std::setw(6) << x

SF64::AnimData::AnimData(int16_t frameCount, int16_t limbCount, uint32_t dataOffset, std::vector<int16_t> frameData, uint32_t keyOffset, std::vector<JointKey> jointKeys): mFrameCount(frameCount), mLimbCount(limbCount), mDataOffset(dataOffset), mFrameData(std::move(frameData)), mKeyOffset(keyOffset), mJointKeys(std::move(jointKeys)) {
    if((mDataOffset + sizeof(mFrameData) > mKeyOffset) && (mKeyOffset + sizeof(mJointKeys) > mDataOffset)) {
        throw std::runtime_error("Data and Key offsets overlap");
    }
    if(mJointKeys.size() != limbCount) {
        throw std::runtime_error("Joint Key count does not match Limb count");
    }
}

void SF64::AnimHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern Animation " << symbol << ";\n";
}

void SF64::AnimCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto anim = std::static_pointer_cast<SF64::AnimData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");

    auto dataOffset = anim->mDataOffset;
    auto keyOffset = anim->mKeyOffset;
    auto dataSegment = 0;
    auto keySegment = 0;

    std::ostringstream dataDefaultName;
    std::ostringstream keyDefaultName;

    if (IS_SEGMENTED(dataOffset)) {
        dataSegment = SEGMENT_NUMBER(dataOffset);
        dataOffset = SEGMENT_OFFSET(dataOffset);
    }

    dataDefaultName << symbol << "_frame_data_" << std::hex << dataOffset;
    auto dataName = GetSafeNode(node, "data_symbol", dataDefaultName.str());

    if (IS_SEGMENTED(keyOffset)) {
        keySegment = SEGMENT_NUMBER(keyOffset);
        keyOffset = SEGMENT_OFFSET(keyOffset);
    }
    keyDefaultName << symbol << "_joint_key_" << std::hex << keyOffset;
    auto keyName = GetSafeNode(node, "data_symbol", keyDefaultName.str());

    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::hex << std::uppercase << offset << "\n";
    }

    write << "u16 " << dataName << "[] = {";
    for(int i = 0; i < anim->mFrameData.size(); i++) {
        if((i % 12) == 0) {
            write << "\n" << fourSpaceTab;
        }
        write << NUM(anim->mFrameData[i]) << ", ";
    }
    write << "\n};\n\n";

    write << "JointKey " << keyName << "[] = {\n";
    for(auto joint : anim->mJointKeys) {
        write << fourSpaceTab << "{";
        for(int i = 0; i < 6; i++) {
            write << NUM(joint.keys[i]) << ", ";
        }
        write << "},\n";
    }
    write << "};\n\n";

    write << "Animation " << symbol << " = {\n";
    write << fourSpaceTab << NUM(anim->mFrameCount) << ", " << NUM(anim->mLimbCount) << ", " << dataName << ", " << keyName << ",\n";
    write << "};\n\n";
}

void SF64::AnimBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto anim = std::static_pointer_cast<AnimData>(raw);
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::AnimData, 0);
    writer.Write(anim->mFrameCount);
    writer.Write(anim->mLimbCount);
    writer.Write((uint32_t) anim->mFrameData.size());
    writer.Write((uint32_t) anim->mJointKeys.size());

    for(auto joint : anim->mJointKeys) {
        for (int i = 0; i < 6; i++) {
            writer.Write(joint.keys[i]);
        }
    }
    for(auto data : anim->mFrameData) {
        writer.Write(data);
    }
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SF64::AnimFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    YAML::Node dataNode;
    YAML::Node keyNode;
    std::vector<SF64::JointKey> jointKeys;
    std::vector<int16_t> frameData;
    auto dataCount = 0;
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0xC);
    LUS::BinaryReader reader(segment.data, segment.size);

    reader.SetEndianness(LUS::Endianness::Big);
    int16_t frameCount = reader.ReadInt16();
    int16_t limbCount = reader.ReadInt16();
    uint32_t dataOffset = reader.ReadUInt32();
    uint32_t keyOffset = reader.ReadUInt32();

    keyNode["offset"] = keyOffset;
    dataNode["offset"] = dataOffset;

    auto [__, keySegment] = Decompressor::AutoDecode(keyNode, buffer, sizeof(SF64::JointKey) * (limbCount + 1));
    LUS::BinaryReader keyReader(keySegment.data, keySegment.size);
    keyReader.SetEndianness(LUS::Endianness::Big);

    for(int i = 0; i <= limbCount; i++) {
        auto xLen = keyReader.ReadUInt16();
        auto x = keyReader.ReadUInt16();
        auto yLen = keyReader.ReadUInt16();
        auto y = keyReader.ReadUInt16();
        auto zLen = keyReader.ReadUInt16();
        auto z = keyReader.ReadUInt16();

        jointKeys.push_back(SF64::JointKey({xLen, x, yLen, y, zLen, z}));
        if(i == 0 || x != 0) {
            dataCount += (xLen < 1) ? 1 : (xLen > frameCount) ? frameCount : xLen;
        }
        if(i == 0 || y != 0) {
            dataCount += (yLen < 1) ? 1 : (yLen > frameCount) ? frameCount : yLen;
        }
        if(i == 0 || z != 0) {
            dataCount += (zLen < 1) ? 1 : (zLen > frameCount) ? frameCount : zLen;
        }
    }

    auto [___, dataSegment] = Decompressor::AutoDecode(dataNode, buffer, sizeof(uint16_t) * dataCount);
    LUS::BinaryReader dataReader(dataSegment.data, dataSegment.size);
    dataReader.SetEndianness(LUS::Endianness::Big);

    for(int i = 0; i < dataCount; i++) {
        frameData.push_back(dataReader.ReadInt16());
    }

    return std::make_shared<AnimData>(frameCount, limbCount, dataOffset, frameData, keyOffset, jointKeys);
}