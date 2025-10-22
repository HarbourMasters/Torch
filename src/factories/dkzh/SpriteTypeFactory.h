#pragma once

#include "factories/BaseFactory.h"
#include <stdint.h>

struct SpriteType {
    int32_t x;
    int32_t y;
    int32_t z;
    int16_t cstat;
    int16_t picnum;
    int16_t sectnum;
    int16_t statnum;
    int16_t ang;
    int16_t unk16;
    int16_t unk18;
    int16_t unk1A;
    int16_t unk1C;
    int16_t lotag;
    int16_t hitag;
    int16_t unk22;
    uint8_t unk24;
    uint8_t unk25;
    uint8_t clipdist;
    uint8_t xrepeat;
    uint8_t yrepeat;
    uint8_t unk29;
    uint8_t unk2A;
    uint8_t unk2B;
};

class SpriteTypeData : public IParsedData {
public:
    std::vector<SpriteType> list;

    explicit SpriteTypeData(const std::vector<SpriteType> &list) : list(list) {}
};
class SpriteTypeHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteTypeBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteTypeCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteTypeFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, SpriteTypeHeaderExporter)
            REGISTER(Binary, SpriteTypeBinaryExporter)
            REGISTER(Code, SpriteTypeCodeExporter)
        };
    }
};
