#pragma once

#include "factories/BaseFactory.h"

namespace AC {

struct BtiTextureData : IParsedData {
    uint16_t width = 0;
    uint16_t height = 0;
    std::vector<uint8_t> rgba;
    std::string archivePath;
};

class BtiTextureBinaryExporter : public BaseExporter {
  public:
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class BtiTextureFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

  private:
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, BtiTextureBinaryExporter) };
    }
};

} // namespace AC
