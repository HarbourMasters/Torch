#pragma once

#include "../BaseFactory.h"

namespace SF64 {

class LimbData : public IParsedData {
public:
    uint32_t mAddr;
    // std::string mSymbol;
    // uint32_t mDList;
    // float mTranslation[3];
    // int16_t mRotation[3];
    uint32_t mDList;
    float mTx;
    float mTy;
    float mTz;
    int16_t mRx;
    int16_t mRy;
    int16_t mRz;
    uint32_t mSibling;
    uint32_t mChild;
    int mIndex;

    LimbData(uint32_t addr, uint32_t dList, float tx, float ty, float tz, float rx, float ry, float rz, uint32_t sibling, uint32_t child, int index);
};

class SkeletonData : public IParsedData {
public:
    std::vector<LimbData> mSkeleton;

    explicit SkeletonData(std::vector<LimbData> skeleton) : mSkeleton(skeleton) {}
};

class SkeletonHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SkeletonBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SkeletonCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SkeletonFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, SkeletonCodeExporter)
            REGISTER(Header, SkeletonHeaderExporter)
            REGISTER(Binary, SkeletonBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};
}
