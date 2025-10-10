#pragma once

#include <factories/BaseFactory.h>
#include <types/Vec3D.h>

namespace FZX {

class EADAnimationData : public IParsedData {
public:
    int16_t mFrameCount;
    int16_t mLimbCount;
    uint32_t mScaleData;
    uint32_t mScaleInfo;
    uint32_t mRotationData;
    uint32_t mRotationInfo;
    uint32_t mPositionData;
    uint32_t mPositionInfo;
    
    EADAnimationData(int16_t frameCount, int16_t limbCount, uint32_t scaleData, uint32_t scaleInfo, uint32_t rotationData, uint32_t rotationInfo, uint32_t positionData, uint32_t positionInfo) :
        mFrameCount(frameCount),
        mLimbCount(limbCount),
        mScaleData(scaleData),
        mScaleInfo(scaleInfo),
        mRotationData(rotationData),
        mRotationInfo(rotationInfo),
        mPositionData(positionData),
        mPositionInfo(positionInfo)
    {}
};

class EADAnimationHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EADAnimationBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EADAnimationCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EADAnimationModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EADAnimationFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, EADAnimationCodeExporter)
            REGISTER(Header, EADAnimationHeaderExporter)
            REGISTER(Binary, EADAnimationBinaryExporter)
        };
    }
};
} // namespace FZX
