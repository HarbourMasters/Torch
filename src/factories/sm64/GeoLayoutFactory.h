#pragma once

#include <factories/BaseFactory.h>
#include "geo/GeoCommand.h"
#include <vector>
#include <variant>

typedef std::variant<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, Vec2f, Vec3f, Vec3s, Vec3i, Vec4f, Vec4s, std::string> GeoArgument;

enum class GeoArgumentType {
    U8, S8, U16, S16, U32, S32, U64, VEC2F, VEC3F, VEC3S, VEC3I, VEC4F, VEC4S, STRING
};

namespace SM64 {

struct GeoCommand {
    GeoOpcode opcode;
    std::vector<GeoArgument> arguments;
    bool skipped;
};

class GeoLayout : public IParsedData {
public:
    std::vector<GeoCommand> commands;

    explicit GeoLayout(std::vector<GeoCommand> commands) : commands(std::move(commands)) {}
};

class GeoCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GeoHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GeoBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GeoLayoutFactory : public BaseFactory {
public:
    GeoLayoutFactory();
    bool CanPreviewCode() override { return true; }

    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, GeoHeaderExporter)
            REGISTER(Binary, GeoBinaryExporter)
            REGISTER(Code, GeoCodeExporter)
        };
    }
};

#ifdef BUILD_UI
// Previews the geo layout as an assembled N64 model: the node tree is flattened
// at bind pose (static transforms, first switch case, branches followed) into
// per-part display lists rendered via Fast3D.
class GeoLayoutFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};
#endif
}
