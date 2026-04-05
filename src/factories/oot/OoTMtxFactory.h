#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <array>

namespace OoT {

class OoTMtxData : public IParsedData {
public:
    std::array<int32_t, 16> rawInts;
    explicit OoTMtxData(std::array<int32_t, 16> data) : rawInts(data) {}
};

class OoTMtxBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTMtxFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTMtxBinaryExporter)
        };
    }
};

} // namespace OoT

#endif
