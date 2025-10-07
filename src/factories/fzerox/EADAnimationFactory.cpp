#include "EADAnimationFactory.h"

#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

#define FORMAT_HEX(x) std::hex << "0x" << std::uppercase << x << std::nouppercase << std::dec
#define FZX_ANIMATION_SIZE 0x1C

ExportResult FZX::EADAnimationHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern EADDemoAnimationInfo " << symbol << ";\n";

    return std::nullopt;
}

ExportResult FZX::EADAnimationCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto anim = std::static_pointer_cast<EADAnimationData>(raw);

    write << "EADDemoAnimationInfo " << symbol << " = {\n";
    write << fourSpaceTab << anim->mFrameCount << ", " << anim->mLimbCount << ", ";

    if (anim->mScaleData == 0) {
        write << "NULL, ";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(anim->mScaleData);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << assetSymbol << ", ";
        } else {
            write << FORMAT_HEX(anim->mScaleData) << ", ";
        }
    }
    if (anim->mScaleInfo == 0) {
        write << "NULL, ";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(anim->mScaleInfo);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << assetSymbol << ", ";
        } else {
            write << FORMAT_HEX(anim->mScaleInfo) << ", ";
        }
    }
    if (anim->mRotationData == 0) {
        write << "NULL, ";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(anim->mRotationData);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << assetSymbol << ", ";
        } else {
            write << FORMAT_HEX(anim->mRotationData) << ", ";
        }
    }
    if (anim->mRotationInfo == 0) {
        write << "NULL, ";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(anim->mRotationInfo);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << assetSymbol << ", ";
        } else {
            write << FORMAT_HEX(anim->mRotationInfo) << ", ";
        }
    }
    if (anim->mPositionData == 0) {
        write << "NULL,";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(anim->mPositionData);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << assetSymbol << ",";
        } else {
            write << FORMAT_HEX(anim->mPositionData) << ",";
        }
    }
    if (anim->mPositionInfo == 0) {
        write << "NULL, ";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(anim->mPositionInfo);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << assetSymbol << ", ";
        } else {
            write << FORMAT_HEX(anim->mPositionInfo) << ", ";
        }
    }
    write << "\n};\n\n";

    return offset + FZX_ANIMATION_SIZE;
}

ExportResult FZX::EADAnimationBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto animation = std::static_pointer_cast<EADAnimationData>(raw);

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> FZX::EADAnimationFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto scaleCount = GetSafeNode<uint32_t>(node, "scale_count");
    const auto rotationCount = GetSafeNode<uint32_t>(node, "rot_count");
    const auto positionCount = GetSafeNode<uint32_t>(node, "pos_count");

    reader.SetEndianness(Torch::Endianness::Big);

    auto frameCount = reader.ReadInt16();
    auto limbCount = reader.ReadInt16();
    auto scaleData = reader.ReadUInt32();
    auto scaleInfo = reader.ReadUInt32();
    auto rotationData = reader.ReadUInt32();
    auto rotationInfo = reader.ReadUInt32();
    auto positionData = reader.ReadUInt32();
    auto positionInfo = reader.ReadUInt32();

    YAML::Node scaleDataNode;
    scaleDataNode["type"] = "ARRAY";
    scaleDataNode["array_type"] = "f32";
    scaleDataNode["count"] = scaleCount;
    scaleDataNode["offset"] = scaleData;
    scaleDataNode["symbol"] = symbol + "ScaleData";
    Companion::Instance->AddAsset(scaleDataNode);

    YAML::Node scaleInfoNode;
    scaleInfoNode["type"] = "ARRAY";
    scaleInfoNode["array_type"] = "s32";
    scaleInfoNode["count"] = 6 * limbCount;
    scaleInfoNode["offset"] = scaleInfo;
    scaleInfoNode["symbol"] = symbol + "ScaleInfo";
    Companion::Instance->AddAsset(scaleInfoNode);

    YAML::Node rotationDataNode;
    rotationDataNode["type"] = "ARRAY";
    rotationDataNode["array_type"] = "s16";
    rotationDataNode["count"] = rotationCount;
    rotationDataNode["offset"] = rotationData;
    rotationDataNode["symbol"] = symbol + "RotationData";
    Companion::Instance->AddAsset(rotationDataNode);

    YAML::Node rotationInfoNode;
    rotationInfoNode["type"] = "ARRAY";
    rotationInfoNode["array_type"] = "s32";
    rotationInfoNode["count"] = 6 * limbCount;
    rotationInfoNode["offset"] = rotationInfo;
    rotationInfoNode["symbol"] = symbol + "RotationInfo";
    Companion::Instance->AddAsset(rotationInfoNode);

    YAML::Node positionDataNode;
    positionDataNode["type"] = "ARRAY";
    positionDataNode["array_type"] = "s16";
    positionDataNode["count"] = positionCount;
    positionDataNode["offset"] = positionData;
    positionDataNode["symbol"] = symbol + "PositionData";
    Companion::Instance->AddAsset(positionDataNode);

    YAML::Node positionInfoNode;
    positionInfoNode["type"] = "ARRAY";
    positionInfoNode["array_type"] = "s32";
    positionInfoNode["count"] = 6 * limbCount;
    positionInfoNode["offset"] = positionInfo;
    positionInfoNode["symbol"] = symbol + "PositionInfo";
    Companion::Instance->AddAsset(positionInfoNode);

    return std::make_shared<EADAnimationData>(frameCount, limbCount, scaleData, scaleInfo, rotationData, rotationInfo, positionData, positionInfo);
}
