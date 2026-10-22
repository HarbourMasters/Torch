#pragma once

#include "factories/BaseFactory.h"
#include "factories/TextureFactory.h"

namespace AC {

class TexAtlasFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, TextureBinaryExporter)
        };
    }
};

} // namespace AC
