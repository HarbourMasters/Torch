#pragma once

#include <factories/BaseFactory.h>

namespace SF64 {

class MessageData : public IParsedData {
public:
    std::vector<uint16_t> mMessage;
    std::vector<std::string> mLines;
    std::string mMesgStr;

    MessageData(std::vector<uint16_t> message, std::string mesgStr, std::vector<std::string> lines) : mMessage(message), mMesgStr(mesgStr), mLines(lines) {}
};

class MessageCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageXMLExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MessageFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(XML, MessageXMLExporter)
            REGISTER(Code, MessageCodeExporter)
            REGISTER(Header, MessageHeaderExporter)
            REGISTER(Binary, MessageBinaryExporter)
            REGISTER(Modding, MessageModdingExporter)
        };
    }
    bool SupportModdedAssets() override { return true; }
};

class MessageFactoryUI : public BaseFactoryUI {
public:
    Vector2 GetBounds(Vector2 windowSize, const ParseResultData& data) override;
    bool DrawUI(Vector2 pos, Vector2 windowSize, const ParseResultData& data) override;
};
}