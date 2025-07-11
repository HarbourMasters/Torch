#include "AnimFactory.h"

#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include "types/RawBuffer.h"

namespace BK64 {

ExportResult AnimHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    return std::nullopt;
}

ExportResult AnimCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<AnimData>(raw);

    return offset;
}

ExportResult BK64::AnimBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto data = std::static_pointer_cast<AnimData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKAnimation, 0);
    
    writer.Write(data->mStartFrame);
    writer.Write(data->mEndFrame);
    writer.Write((uint32_t)data->mFiles.size());

    for (const auto& file : data->mFiles) {
        writer.Write(file.mBoneIndex);
        writer.Write(file.mTransformType);
        writer.Write((uint32_t)file.mData.size());
        for (const auto& fileData : file.mData) {
            writer.Write(fileData.unk0_15);
            writer.Write(fileData.unk0_14);
            writer.Write(fileData.unk0_13);
            writer.Write(fileData.unk2);
        }
    }
    
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> AnimFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");

    SPDLOG_ERROR("{} START", symbol);

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

} // namespace BK64
