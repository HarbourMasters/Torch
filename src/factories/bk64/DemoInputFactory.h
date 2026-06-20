#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

typedef struct ControllerInput {
    int8_t stickX;
    int8_t stickY;
    uint16_t buttons;
    uint8_t frames;
    uint8_t unkFlag;
} ControllerInput;

class DemoInputData : public IParsedData {
  public:
    std::vector<ControllerInput> mInputs;

    DemoInputData(std::vector<ControllerInput> inputs) : mInputs(std::move(inputs)) {
    }
};

class DemoInputHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DemoInputBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DemoInputCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DemoInputModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DemoInputFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, DemoInputCodeExporter) REGISTER(Header, DemoInputHeaderExporter)
                     REGISTER(Binary, DemoInputBinaryExporter) REGISTER(Modding, DemoInputModdingExporter) };
    }
    bool SupportModdedAssets() override {
        return true;
    }
};
} // namespace BK64
