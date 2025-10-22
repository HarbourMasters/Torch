#pragma once

#include "factories/BaseFactory.h"

struct VertexRaw {
    int16_t		ob[3];	/* x, y, z */
    int16_t		tc[2];	/* texture coord */
    uint8_t	    cn[4];	/* color & alpha */
};

class VertexData : public IParsedData {
public:
    std::vector<VertexRaw> mVtxs;

    explicit VertexData(std::vector<VertexRaw> vtxs) : mVtxs(vtxs) {}
};

class VertexHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class VertexBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class VertexCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class VertexFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, VertexCodeExporter)
            REGISTER(Header, VertexHeaderExporter)
            REGISTER(Binary, VertexBinaryExporter)
        };
    }
    uint32_t GetAlignment() override {
        return 8;
    };
};