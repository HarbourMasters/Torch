#pragma once

#include "BaseFactory.h"
#include "n64/CommandMacros.h"

#define F3DEX
#define _LANGUAGE_C
#include "n64/gbi.h"

class DListData : public IParsedData {
public:
    std::vector<uint32_t> mGfxs;

    explicit DListData(std::vector<uint32_t> gfxs) : mGfxs(gfxs) {}
};

class DListHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class DListBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class DListCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class DListFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, DListHeaderExporter)
            REGISTER(Binary, DListBinaryExporter)
            REGISTER(Code, DListCodeExporter)
        };
    }
};