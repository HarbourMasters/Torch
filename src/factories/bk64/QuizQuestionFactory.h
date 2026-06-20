#pragma once

#include "DialogFactory.h"
#include <factories/BaseFactory.h>

namespace BK64 {

class QuizQuestionData : public IParsedData {
  public:
    std::vector<DialogString> mText;
    std::vector<DialogString> mOptions;

    QuizQuestionData(std::vector<DialogString> text, std::vector<DialogString> options)
        : mText(std::move(text)), mOptions(std::move(options)) {
    }
};

class QuizQuestionHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class QuizQuestionBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class QuizQuestionCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class QuizQuestionModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class QuizQuestionFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, QuizQuestionCodeExporter) REGISTER(Header, QuizQuestionHeaderExporter)
                     REGISTER(Binary, QuizQuestionBinaryExporter) REGISTER(Modding, QuizQuestionModdingExporter) };
    }
    bool SupportModdedAssets() override {
        return true;
    }
};
} // namespace BK64
