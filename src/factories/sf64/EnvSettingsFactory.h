#pragma once

#include "../BaseFactory.h"
#include "types/Vec3D.h"

namespace SF64 {

class EnvSettingsData : public IParsedData {
public:
    int32_t  mType;
    int32_t  mUnk_04;
    uint16_t mBgColor;
    uint16_t mSeqId;
    int32_t  mFogR;
    int32_t  mFogG;
    int32_t  mFogB;
    int32_t  mFogN;
    int32_t  mFogF;
    Vec3f    mUnk_20;
    int32_t  mLightR;
    int32_t  mLightG;
    int32_t  mLightB;
    int32_t  mAmbR;
    int32_t  mAmbG;
    int32_t  mAmbB;

    EnvSettingsData(int32_t type, int32_t unk_04, uint16_t bgColor, uint16_t seqId, int32_t fogR, int32_t fogG, int32_t fogB, int32_t fogN, int32_t fogF, Vec3f unk_20, int32_t lightR, int32_t lightG, int32_t lightB, int32_t ambR, int32_t ambG, int32_t ambB);
};

class EnvSettingsHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EnvSettingsBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EnvSettingsCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EnvSettingsFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, EnvSettingsCodeExporter)
            REGISTER(Header, EnvSettingsHeaderExporter)
            REGISTER(Binary, EnvSettingsBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};
}
