#pragma once

#include "../BaseFactory.h"

namespace SF64 {

class HitboxData : public IParsedData {
public:
    std::vector<float> mData;
    std::vector<int> mTypes;

    HitboxData(std::vector<float> data, std::vector<int> types);
};

class HitboxHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class HitboxBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class HitboxCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class HitboxFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, HitboxCodeExporter)
            REGISTER(Header, HitboxHeaderExporter)
            REGISTER(Binary, HitboxBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};
}
