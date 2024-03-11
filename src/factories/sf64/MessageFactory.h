#pragma once

#include "../BaseFactory.h"

namespace SF64 {

class MessageData : public IParsedData {
public:
    std::vector<uint8_t> mMessage;

    MessageData(std::vector<uint8_t> message) : mMessage(message) {}
};

class MessageCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, MessageCodeExporter)
            REGISTER(Header, MessageHeaderExporter)
            REGISTER(Binary, MessageBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};
}