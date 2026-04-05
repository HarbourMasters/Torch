#pragma once

#include "factories/BaseFactory.h"

namespace OoT {

// Handles OOT:TEXT type (OTXT 0x4F545854), message tables + per-language data.
// Reference: Shipwright's TextFactory.cpp for binary format.

class OoTTextBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTTextFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& node) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTTextBinaryExporter)
        };
    }
};

}  // namespace OoT
