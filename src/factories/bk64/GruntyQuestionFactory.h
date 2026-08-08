#pragma once

#include "DialogFactory.h"
#include <factories/BaseFactory.h>
#include "factories/bk64/BKHeaderExporter.h"

namespace BK64 {

typedef struct OptionString {
    uint8_t cmd;
    uint8_t unk0;
    uint8_t unk1;
    std::string str;
} OptionString;

// One language's question: the text entries plus the three answer options.
typedef struct GruntyQuestionLang {
    std::vector<DialogString> text;
    std::vector<OptionString> options;
} GruntyQuestionLang;

class GruntyQuestionData : public IParsedData {
  public:
    // Always set. English on PAL, the lone language on US.
    std::vector<DialogString> mText;
    std::vector<OptionString> mOptions;

    // PAL only: index 0=French, 1=German
    std::vector<GruntyQuestionLang> mExtraLangs;

    GruntyQuestionData(std::vector<DialogString> text, std::vector<OptionString> options)
        : mText(std::move(text)), mOptions(std::move(options)) {
    }

    GruntyQuestionData(std::vector<DialogString> text, std::vector<OptionString> options,
                       std::vector<GruntyQuestionLang> extraLangs)
        : mText(std::move(text)), mOptions(std::move(options)), mExtraLangs(std::move(extraLangs)) {
    }
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
        return { REGISTER(Code, GruntyQuestionCodeExporter) REGISTER(Header, BKHeaderExporter)
                     REGISTER(Binary, GruntyQuestionBinaryExporter) REGISTER(Modding, GruntyQuestionModdingExporter) };
    }
    bool SupportModdedAssets() override {
        return true;
    }
};
} // namespace BK64
