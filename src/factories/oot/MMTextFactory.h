#pragma once

#include "factories/BaseFactory.h"

namespace OoT {

// One entry of Majora's Mask's message table. Mirrors MessageEntryMM in
// ZAPDTR/ZAPD/ZTextMM.h -- the fields, and their widths, are what the exporter
// writes.
struct MMMessageEntry {
    uint16_t id = 0;
    uint8_t textboxType = 0;
    uint8_t textboxYPos = 0;
    uint16_t icon = 0;
    uint16_t nextMessageID = 0;
    uint16_t firstItemCost = 0;
    uint16_t secondItemCost = 0;
    std::string msg;
};

class MMTextBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MMTextFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, MMTextBinaryExporter)
        };
    }
};

} // namespace OoT
