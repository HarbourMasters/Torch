#pragma once

#include "DialogFactory.h"
#include <factories/BaseFactory.h>

namespace BK64 {

typedef struct OptionString {
    uint8_t cmd;
    uint8_t unk0;
    uint8_t unk1;
    std::string str;
} OptionString;

class GruntyQuestionData : public IParsedData {
  public:
    std::vector<DialogString> mText;
    std::vector<OptionString> mOptions;

    GruntyQuestionData(std::vector<DialogString> text, std::vector<OptionString> options)
        : mText(std::move(text)), mOptions(std::move(options)) {
    }
};

class GruntyQuestionHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class GruntyQuestionBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class GruntyQuestionCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class GruntyQuestionModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class GruntyQuestionFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, GruntyQuestionCodeExporter) REGISTER(Header, GruntyQuestionHeaderExporter)
                     REGISTER(Binary, GruntyQuestionBinaryExporter) REGISTER(Modding, GruntyQuestionModdingExporter) };
    }
    bool SupportModdedAssets() override {
        return true;
    }
};
} // namespace BK64
