#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

typedef union { //transform structure [xx xx] ⇒ [DE FF FF FF FF FF FF FF]
    struct {
        uint8_t unk0_15 : 1;  // Bitwise Parameters 1 [D]
        uint8_t unk0_14 : 1;  // Bitwise Parameters 2 [E]
        uint16_t unk0_13 : 14; // frame of transformation [FF FF FF FF FF FF FF], somehow 87.59375 (0x15E6) after bitwise operations
        int16_t unk2;
    };
} AnimationFileData;

class AnimationFile {
public:
    int16_t mBoneIndex;
    int16_t mTransformType;
    std::vector<AnimationFileData> mData;

    AnimationFile(int16_t boneIndex, int16_t transformType, std::vector<AnimationFileData> data) : mBoneIndex(boneIndex), mTransformType(transformType), mData(std::move(data)) {}
};

class AnimData : public IParsedData {
  public:
    int16_t mStartFrame;
    int16_t mEndFrame;
    std::vector<AnimationFile> mFiles;

    AnimData(int16_t startFrame, int16_t endFrame, std::vector<AnimationFile> files) : mStartFrame(startFrame), mEndFrame(endFrame), mFiles(std::move(files)) {}
};

class AnimHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AnimBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AnimCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AnimFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { 
            REGISTER(Code, AnimCodeExporter) 
            REGISTER(Header, AnimHeaderExporter)                     
            REGISTER(Binary, AnimBinaryExporter) 
        };
    }
};
} // namespace BK64
