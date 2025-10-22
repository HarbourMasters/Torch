#pragma once

#include "factories/BaseFactory.h"
#include <stdint.h>

struct WallType {
    int32_t x;
    int32_t y;
    int16_t point2;
    int16_t nextwall;
    int16_t nextsector;
    int16_t cstat;
    int16_t picnum;
    int16_t overpicnum;
    int16_t unk14;
    int16_t unk16;
    int16_t unk18;
    uint16_t sectnum;
    uint8_t shade;
    uint8_t unk1D;
    uint8_t unk1E;
    uint8_t unk1F;
    uint8_t unk20;
    uint8_t pal;
    uint8_t xrepeat;
    uint8_t yrepeat;
    uint8_t xpanning;
    uint8_t ypanning;
    uint8_t pad3[2];
};

class WallTypeData : public IParsedData {
public:
    std::vector<WallType> list;

    explicit WallTypeData(const std::vector<WallType> &list) : list(list) {}
};
class WallTypeHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class WallTypeBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class WallTypeCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class WallTypeFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, WallTypeHeaderExporter)
            REGISTER(Binary, WallTypeBinaryExporter)
            REGISTER(Code, WallTypeCodeExporter)
        };
    }
};
