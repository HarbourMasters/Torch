#pragma once

#include "factories/BaseFactory.h"

namespace MM {

class MMPathBinaryExporter : public BaseExporter {
public:
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MMPathFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

private:
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, MMPathBinaryExporter) };
    }
};

} // namespace MM
