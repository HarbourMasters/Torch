#include "AnimFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

namespace BK64 {

ExportResult AnimHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                        YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    return std::nullopt;
}

ExportResult AnimCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                      YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto anim = std::static_pointer_cast<AnimData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "AnimationFile " << symbol << "_File = { " << anim->mStartFrame << ", " << anim->mEndFrame << ", "
          << anim->mFiles.size() << " };\n\n";

    write << "AnimationFileElement " << symbol << "_Data[] = {\n";
    for (const auto& file : anim->mFiles) {
        write << fourSpaceTab << "{\n";
        write << fourSpaceTab << fourSpaceTab << file.mBoneIndex << ", " << file.mTransformType << ", "
              << file.mData.size() << ",\n";
        for (const auto& fileData : file.mData) {
            write << fourSpaceTab << fourSpaceTab << "{ ";
            write << (uint32_t)fileData.unk0_15 << ", " << (uint32_t)fileData.unk0_14 << ", " << fileData.unk0_13
                  << ", " << fileData.unk2;
            write << " },\n";
        }
        write << fourSpaceTab << "},\n";
    }

    write << "};\n\n";

    return offset;
}

ExportResult BK64::AnimBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto anim = std::static_pointer_cast<AnimData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKAnimation, 0);

    writer.Write(anim->mStartFrame);
    writer.Write(anim->mEndFrame);
    writer.Write((uint32_t)anim->mFiles.size());

    for (const auto& file : anim->mFiles) {
        writer.Write(file.mBoneIndex);
        writer.Write(file.mTransformType);
        writer.Write((uint32_t)file.mData.size());
        for (const auto& fileData : file.mData) {
            writer.Write(static_cast<uint8_t>(fileData.unk0_15));
            writer.Write(static_cast<uint8_t>(fileData.unk0_14));
            writer.Write(static_cast<uint16_t>(fileData.unk0_13));
            writer.Write(fileData.unk2);
        }
    }

    writer.Finish(write);
    return std::nullopt;
}

ExportResult BK64::AnimModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto anim = std::static_pointer_cast<AnimData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);

    out << YAML::BeginMap;
    out << YAML::Key << "StartFrame";
    out << YAML::Value << anim->mStartFrame;
    out << YAML::Key << "EndFrame";
    out << YAML::Value << anim->mEndFrame;
    out << YAML::Key << "Elements";
    out << YAML::Value;

    out << YAML::BeginSeq;
    for (const auto& file : anim->mFiles) {
        out << YAML::BeginMap;
        out << YAML::Key << "BoneIndex";
        out << YAML::Value << file.mBoneIndex;
        out << YAML::Key << "TransformType";
        out << YAML::Value << file.mTransformType;
        out << YAML::Key << "Data";
        out << YAML::Value;
        out << YAML::BeginSeq;
        for (const auto& fileData : file.mData) {
            out << YAML::Flow << YAML::BeginSeq;
            out << YAML::Value << (uint32_t)fileData.unk0_15;
            out << YAML::Value << (uint32_t)fileData.unk0_14;
            out << YAML::Value << fileData.unk0_13;
            out << YAML::Value << fileData.unk2;
            out << YAML::EndSeq;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> AnimFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");

    int16_t startFrame = reader.ReadInt16();
    int16_t endFrame = reader.ReadInt16();
    int16_t fileCount = reader.ReadInt16();
    reader.ReadInt16();

    std::vector<AnimationFile> animFiles;
    for (int16_t i = 0; i < fileCount; i++) {
        int16_t fileElementBitField = reader.ReadInt16();
        int16_t dataCount = reader.ReadInt16();
        int16_t boneIndex = (fileElementBitField & 0xFFF0) >> 4;
        int16_t transformType = fileElementBitField & 0xF;

        std::vector<AnimationFileData> fileData;

        for (int16_t j = 0; j < dataCount; j++) {
            AnimationFileData data;
            int16_t fileDataBitField = reader.ReadInt16();

            data.unk0_15 = (fileDataBitField & 0b1000000000000000) >> 15;
            data.unk0_14 = (fileDataBitField & 0b0100000000000000) >> 14;
            data.unk0_13 = fileDataBitField & 0b0011111111111111;
            data.unk2 = reader.ReadInt16();

            fileData.emplace_back(data);
        }

        animFiles.emplace_back(boneIndex, transformType, fileData);
    }

    return std::make_shared<AnimData>(startFrame, endFrame, animFiles);
}

std::optional<std::shared_ptr<IParsedData>> AnimFactory::parse_modding(std::vector<uint8_t>& buffer, YAML::Node& node) {
    YAML::Node assetNode;

    try {
        std::string text((char*)buffer.data(), buffer.size());
        assetNode = YAML::Load(text.c_str());
    } catch (YAML::ParserException& e) {
        SPDLOG_ERROR("Failed to parse message data: {}", e.what());
        SPDLOG_ERROR("{}", (char*)buffer.data());
        return std::nullopt;
    }

    const auto info = assetNode.begin()->second;

    auto startFrame = info["StartFrame"].as<int16_t>();
    auto endFrame = info["EndFrame"].as<int16_t>();

    auto elements = info["Elements"];

    std::vector<AnimationFile> animFiles;

    for (YAML::iterator it = elements.begin(); it != elements.end(); ++it) {
        auto elem = *it;

        auto boneIndex = elem["BoneIndex"].as<int16_t>();
        auto transformType = elem["TransformType"].as<int16_t>();
        auto data = elem["Data"];

        std::vector<AnimationFileData> fileData;
        for (YAML::iterator jt = data.begin(); jt != data.end(); ++jt) {
            AnimationFileData animFileData;

            animFileData.unk0_15 = (*jt)[0].as<uint32_t>();
            animFileData.unk0_14 = (*jt)[1].as<uint32_t>();
            animFileData.unk0_13 = (*jt)[2].as<uint16_t>();
            animFileData.unk2 = (*jt)[3].as<int16_t>();

            fileData.push_back(animFileData);
        }
        animFiles.emplace_back(boneIndex, transformType, fileData);
    }

    return std::make_shared<AnimData>(startFrame, endFrame, animFiles);
}

} // namespace BK64
