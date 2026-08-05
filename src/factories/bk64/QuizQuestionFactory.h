#pragma once

#include "DialogFactory.h"
#include <factories/BaseFactory.h>
#include "factories/bk64/BKHeaderExporter.h"

namespace BK64 {

// One language's question: the text entries plus the three answer options.
typedef struct QuizQuestionLang {
    std::vector<DialogString> text;
    std::vector<DialogString> options;
} QuizQuestionLang;

class QuizQuestionData : public IParsedData {
  public:
    // Always set. English on PAL, the lone language on US.
    std::vector<DialogString> mText;
    std::vector<DialogString> mOptions;

    // PAL only: index 0=French, 1=German
    std::vector<QuizQuestionLang> mExtraLangs;

    QuizQuestionData(std::vector<DialogString> text, std::vector<DialogString> options)
        : mText(std::move(text)), mOptions(std::move(options)) {
    }

    QuizQuestionData(std::vector<DialogString> text, std::vector<DialogString> options,
                     std::vector<QuizQuestionLang> extraLangs)
        : mText(std::move(text)), mOptions(std::move(options)), mExtraLangs(std::move(extraLangs)) {
    }
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
        return { REGISTER(Code, QuizQuestionCodeExporter) REGISTER(Header, BKHeaderExporter)
                     REGISTER(Binary, QuizQuestionBinaryExporter) REGISTER(Modding, QuizQuestionModdingExporter) };
    }
    bool SupportModdedAssets() override {
        return true;
    }
};
} // namespace BK64
