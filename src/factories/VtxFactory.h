#pragma once

#include "BaseFactory.h"

struct VtxRaw {
    int16_t		ob[3];	/* x, y, z */
    uint16_t	flag;
    int16_t		tc[2];	/* texture coord */
    uint8_t	    cn[4];	/* color & alpha */
};

class VtxData : public IParsedData {
public:
    std::vector<VtxRaw> mVtxs;

    explicit VtxData(std::vector<VtxRaw> vtxs) : mVtxs(vtxs) {}
};

class VtxHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class VtxBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class VtxCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class VtxFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, VtxCodeExporter)
            REGISTER(Header, VtxHeaderExporter)
            REGISTER(Binary, VtxBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};