#pragma once

#include "factories/BaseFactory.h"
#include "SectorTypeFactory.h"

struct MapInfo {
    uint32_t rom_start;
    uint32_t rom_end;
    int32_t sector_offset;
    int32_t wall_offset;
    int32_t sprite_offset;
    int32_t sectors;
    int32_t sprites;
    int32_t walls;
    int32_t xpos;
    int32_t ypos;
    int32_t zpos;
    int32_t ang;
    float skytop_r;
    float skytop_g;
    float skytop_b;
    float skybottom_r;
    float skybottom_g;
    float skybottom_b;
};

class MapInfoData : public IParsedData {
public:
    std::vector<MapInfo> list;

    explicit MapInfoData(const std::vector<MapInfo> &list) : list(list) {}
};

class MapInfoHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MapInfoBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MapInfoCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MapInfoFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, MapInfoHeaderExporter)
            REGISTER(Binary, MapInfoBinaryExporter)
            REGISTER(Code, MapInfoCodeExporter)
        };
    }
};
