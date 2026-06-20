#pragma once

#include "factories/BaseFactory.h"
#include "factories/BlobFactory.h"

namespace BK64 {
class SoundfontTblFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Header, BlobHeaderExporter) REGISTER(Binary, BlobBinaryExporter)
                     REGISTER(Code, BlobCodeExporter) };
    }
};

} // namespace BK64
