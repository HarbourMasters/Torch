#pragma once

#include "factories/BaseFactory.h"
#include <stdint.h>

struct SectorType {
    int32_t ceilingz;
    int32_t floorz;
    int16_t wallptr;
    int16_t wallnum;
    int16_t ceilingstat;
    int16_t floorstat;
    int16_t ceilingpicnum;
    int16_t ceilingheinum;
    int16_t floorpicnum;
    int16_t floorheinum;
    int16_t unk18; /*lotag?*/
    int16_t unk1A; /*hitag?*/
    int16_t unk1C; /*extra?*/
    uint16_t floorvtxptr;
    uint16_t ceilingvtxptr;
    uint8_t ceilingshade;
    uint8_t ceilingpal;
    uint8_t pad[2];
    uint8_t floorshade;
    uint8_t floorpal;
    uint8_t pad2[2];
    uint8_t unk2A;
    uint8_t floorvtxnum;
    uint8_t ceilingvtxnum;
    uint8_t pad3[3];
};

class SectorTypeData : public IParsedData {
public:
    std::vector<SectorType> list;

    explicit SectorTypeData(const std::vector<SectorType> &list) : list(list) {}
};
class SectorTypeHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SectorTypeBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SectorTypeCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SectorTypeFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, SectorTypeHeaderExporter)
            REGISTER(Binary, SectorTypeBinaryExporter)
            REGISTER(Code, SectorTypeCodeExporter)
        };
    }
};
