#pragma once

#include "factories/BaseFactory.h"

namespace AC {

struct PlayerClothTextureData : IParsedData {
    std::vector<uint8_t> rgba;
    std::string archivePath;
};

class PlayerClothTextureBinaryExporter : public BaseExporter {
  public:
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class PlayerClothTextureFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

  private:
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, PlayerClothTextureBinaryExporter) };
    }
};

} // namespace AC
