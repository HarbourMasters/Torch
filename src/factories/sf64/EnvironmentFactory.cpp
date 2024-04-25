#include "EnvironmentFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#define VALUE_TO_ENUM(val, enumname, fallback) (Companion::Instance->GetEnumFromValue(enumname, val).value_or("/*" + std::string(fallback) + " */ " + std::to_string(val)));

SF64::EnvironmentData::EnvironmentData(int32_t type, int32_t ground, uint16_t bgColor, uint16_t seqId, int32_t fogR, int32_t fogG, int32_t fogB, int32_t fogN, int32_t fogF, Vec3f lightDir, int32_t lightR, int32_t lightG, int32_t lightB, int32_t ambR, int32_t ambG, int32_t ambB): mType(type), mGround(ground), mBgColor(bgColor), mSeqId(seqId), mFogR(fogR), mFogG(fogG), mFogB(fogB), mFogN(fogN), mFogF(fogF), mLightDir(lightDir), mLightR(lightR), mLightG(lightG), mLightB(lightB), mAmbR(ambR), mAmbG(ambG), mAmbB(ambB) {

}

ExportResult SF64::EnvironmentHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Environment " << symbol << ";\n";
    return std::nullopt;
}

ExportResult SF64::EnvironmentCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto env = std::static_pointer_cast<SF64::EnvironmentData>(raw);

    write << "Environment " << symbol << " = {\n";
    write << fourSpaceTab;
    auto type = VALUE_TO_ENUM(env->mType, "LevelType", "LEVELTYPE_UNK");
    write << type << ", ";
    auto ground = VALUE_TO_ENUM(env->mGround, "GroundType", "GROUND_UNK");
    write << ground << ", ";
    write << "0x" << std::hex << std::uppercase << env->mBgColor << ", ";
    if (env->mSeqId == 0xFFFF) {
        write << "SEQ_ID_NONE, ";
    } else {
        auto seqId = Companion::Instance->GetEnumFromValue("BgmSeqIds", env->mSeqId & 0xFF).value_or("/* SEQ_ID_UNK */ " + std::to_string(env->mSeqId));
        write << seqId << ((env->mSeqId < 0x8000) ? "" : " | SEQ_FLAG") << ", ";
    }
    write << std::dec << env->mFogR << ", ";
    write << env->mFogG << ", ";
    write << env->mFogB << ", ";
    write << env->mFogN << ", ";
    write << env->mFogF << ", ";
    write << env->mLightDir << ", ";
    write << env->mLightR << ", ";
    write << env->mLightG << ", ";
    write << env->mLightB << ", ";
    write << env->mAmbR << ", ";
    write << env->mAmbG << ", ";
    write << env->mAmbB << ",\n";
    write << "};\n";

    return offset + sizeof(EnvironmentData);
}

ExportResult SF64::EnvironmentBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto environment = std::static_pointer_cast<SF64::EnvironmentData>(raw);

    WriteHeader(writer, LUS::ResourceType::Environment, 0);

    writer.Write(environment->mType);
    writer.Write(environment->mGround);
    writer.Write(environment->mBgColor);
    writer.Write(environment->mSeqId);
    writer.Write(environment->mFogR);
    writer.Write(environment->mFogG);
    writer.Write(environment->mFogB);
    writer.Write(environment->mFogN);
    writer.Write(environment->mFogF);
    writer.Write(environment->mLightDir.x);
    writer.Write(environment->mLightDir.y);
    writer.Write(environment->mLightDir.z);
    writer.Write(environment->mLightR);
    writer.Write(environment->mLightG);
    writer.Write(environment->mLightB);
    writer.Write(environment->mAmbR);
    writer.Write(environment->mAmbG);
    writer.Write(environment->mAmbB);

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SF64::EnvironmentFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, sizeof(SF64::EnvironmentData));
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);
    Vec3f lightDir;
    int32_t  type = reader.ReadInt32();
    int32_t  ground = reader.ReadInt32();
    uint16_t bgColor = reader.ReadUInt16();
    uint16_t seqId = reader.ReadUInt16();
    int32_t  fogR = reader.ReadInt32();
    int32_t  fogG = reader.ReadInt32();
    int32_t  fogB = reader.ReadInt32();
    int32_t  fogN = reader.ReadInt32();
    int32_t  fogF = reader.ReadInt32();
    lightDir.x = reader.ReadFloat();
    lightDir.y = reader.ReadFloat();
    lightDir.z = reader.ReadFloat();
    int32_t  lightR = reader.ReadInt32();
    int32_t  lightG = reader.ReadInt32();
    int32_t  lightB = reader.ReadInt32();
    int32_t  ambR = reader.ReadInt32();
    int32_t  ambG = reader.ReadInt32();
    int32_t  ambB = reader.ReadInt32();

    return std::make_shared<SF64::EnvironmentData>(type, ground, bgColor, seqId, fogR, fogG, fogB, fogN, fogF, lightDir, lightR, lightG, lightB, ambR, ambG, ambB);
}
