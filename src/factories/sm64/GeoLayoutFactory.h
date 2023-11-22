#pragma once

#include "../BaseFactory.h"
#include "geo/GeoCommand.h"

namespace SM64 {

typedef std::variant<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, Vec2f, Vec3f, Vec3s, Vec3i, Vec4f, Vec4s> GeoArgument;

struct GeoCommand {
    GeoOpcode opcode;
    std::vector<GeoArgument> arguments;
};

class GeoLayout : public IParsedData {
public:
    std::vector<GeoCommand> commands;

    explicit GeoLayout(std::vector<GeoCommand> commands) : commands(std::move(commands)) {}
};

class GeoHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GeoBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GeoLayoutFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, GeoHeaderExporter)
            REGISTER(Binary, GeoBinaryExporter)
        };
    }
};
}
