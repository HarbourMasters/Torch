#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

typedef struct DialogString {
    uint8_t cmd;
    std::string str;
} DialogString;

// One language's dialog: the bottom text box plus the top one.
typedef struct DialogLang {
    std::vector<DialogString> bottom;
    std::vector<DialogString> top;
} DialogLang;

class DialogData : public IParsedData {
  public:
    // Always set. English on PAL/JP, the lone language on US.
    std::vector<DialogString> mBottom;
    std::vector<DialogString> mTop;

    // PAL only: index 0=French, 1=German
    std::vector<DialogLang> mExtraLangs;

    DialogData(std::vector<DialogString> bottom, std::vector<DialogString> top)
        : mBottom(std::move(bottom)), mTop(std::move(top)) {
    }

    DialogData(std::vector<DialogString> bottom, std::vector<DialogString> top, std::vector<DialogLang> extraLangs)
        : mBottom(std::move(bottom)), mTop(std::move(top)), mExtraLangs(std::move(extraLangs)) {
    }
};

class DialogHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DialogBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DialogCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DialogModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DialogFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, DialogCodeExporter) REGISTER(Header, DialogHeaderExporter)
                     REGISTER(Binary, DialogBinaryExporter) REGISTER(Modding, DialogModdingExporter) };
    }
    bool SupportModdedAssets() override {
        return true;
    }
};
} // namespace BK64
