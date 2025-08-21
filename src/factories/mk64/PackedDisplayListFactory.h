#pragma once

#include "../BaseFactory.h"
#include "../DisplayListFactory.h" // Reuse DList exporters and data shape

// Factory to expand MK64 packed DL bytecode into regular Gfx commands
class PackedDListFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, DListHeaderExporter)
            REGISTER(Binary, DListBinaryExporter)
        };
    }
    uint32_t GetAlignment() override { return 0; }
};
