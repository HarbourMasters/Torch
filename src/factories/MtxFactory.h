#pragma once

#include "BaseFactory.h"

struct MtxRaw {
    uint32_t mtx[4][4];
};

class MtxData : public IParsedData {
public:
    std::vector<MtxRaw> mMtxs;

    explicit MtxData(std::vector<MtxRaw> mtxs) : mMtxs(mtxs) {}
};

class MtxHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MtxBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MtxCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MtxFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, MtxCodeExporter)
            REGISTER(Header, MtxHeaderExporter)
            REGISTER(Binary, MtxBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};