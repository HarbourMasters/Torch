#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"

namespace OoT {

class OoTCutsceneBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTCutsceneFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTCutsceneBinaryExporter)
        };
    }
};

} // namespace OoT

#endif
