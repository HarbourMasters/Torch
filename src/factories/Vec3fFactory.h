#pragma once

#include "BaseFactory.h"
#include "types/Vec3D.h"

class Vec3fData : public IParsedData {
public:
    std::vector<Vec3f> mVecs;
    int mMaxWidth;
    int mMaxPrec;

    explicit Vec3fData(std::vector<Vec3f> vecs);
};

class Vec3fHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class Vec3fBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class Vec3fCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class Vec3fFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, Vec3fCodeExporter)
            REGISTER(Header, Vec3fHeaderExporter)
            REGISTER(Binary, Vec3fBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};
