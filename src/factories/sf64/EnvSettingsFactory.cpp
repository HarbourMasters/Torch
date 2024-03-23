#include "EnvSettingsFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

SF64::EnvSettingsData::EnvSettingsData(int32_t type, int32_t unk_04, uint16_t bgColor, uint16_t seqId, int32_t fogR, int32_t fogG, int32_t fogB, int32_t fogN, int32_t fogF, Vec3f unk_20, int32_t lightR, int32_t lightG, int32_t lightB, int32_t ambR, int32_t ambG, int32_t ambB): mType(type), mUnk_04(unk_04), mBgColor(bgColor), mSeqId(seqId), mFogR(fogR), mFogG(fogG), mFogB(fogB), mFogN(fogN), mFogF(fogF), mUnk_20(unk_20), mLightR(lightR), mLightG(lightG), mLightB(lightB), mAmbR(ambR), mAmbG(ambG), mAmbB(ambB) {

}

ExportResult SF64::EnvSettingsHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern EnvSettings " << symbol << ";\n";
    return std::nullopt;
}

ExportResult SF64::EnvSettingsCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto env = std::static_pointer_cast<SF64::EnvSettingsData>(raw);

    write << "EnvSettings " << symbol << " = {\n";
    write << fourSpaceTab;
    write << std::dec << env->mType << ", ";
    write << env->mUnk_04 << ", ";
    write << env->mBgColor << ", ";
    if(env->mSeqId < 0x8000) {
        write << env->mSeqId << ", ";
    } else {
        write << env->mSeqId - 0x8000 << " | 0x8000, ";
    }
    write << env->mFogR << ", ";
    write << env->mFogG << ", ";
    write << env->mFogB << ", ";
    write << env->mFogN << ", ";
    write << env->mFogF << ", ";
    write << env->mUnk_20 << ", ";
    write << env->mLightR << ", ";
    write << env->mLightG << ", ";
    write << env->mLightB << ", ";
    write << env->mAmbR << ", ";
    write << env->mAmbG << ", ";
    write << env->mAmbB << ",\n";
    write << "};\n";

    return offset + sizeof(EnvSettingsData);
}

ExportResult SF64::EnvSettingsBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SF64::EnvSettingsFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, sizeof(SF64::EnvSettingsData));
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);
    Vec3f unk_20;
    int32_t  type = reader.ReadInt32();
    int32_t  unk_04 = reader.ReadInt32();
    uint16_t bgColor = reader.ReadUInt16();
    uint16_t seqId = reader.ReadUInt16();
    int32_t  fogR = reader.ReadInt32();
    int32_t  fogG = reader.ReadInt32();
    int32_t  fogB = reader.ReadInt32();
    int32_t  fogN = reader.ReadInt32();
    int32_t  fogF = reader.ReadInt32();
    unk_20.x = reader.ReadFloat();
    unk_20.y = reader.ReadFloat();
    unk_20.z = reader.ReadFloat();
    int32_t  lightR = reader.ReadInt32();
    int32_t  lightG = reader.ReadInt32();
    int32_t  lightB = reader.ReadInt32();
    int32_t  ambR = reader.ReadInt32();
    int32_t  ambG = reader.ReadInt32();
    int32_t  ambB = reader.ReadInt32();

    return std::make_shared<SF64::EnvSettingsData>(type, unk_04, bgColor, seqId, fogR, fogG, fogB, fogN, fogF, unk_20, lightR, lightG, lightB, ambR, ambG, ambB);
}
