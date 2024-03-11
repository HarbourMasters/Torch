#pragma once

#include "../BaseFactory.h"

namespace SF64 {

struct MessageEntry {
    int32_t id;
    uint32_t ptr;
};

class MessageTable : public IParsedData {
public:
    std::vector<MessageEntry> mTable;

    MessageTable(const std::vector<MessageEntry>&table) : mTable(table) {}
};

class MessageLookupCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageLookupHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageLookupBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageLookupFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, MessageLookupCodeExporter)
            REGISTER(Header, MessageLookupHeaderExporter)
            REGISTER(Binary, MessageLookupBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};
}