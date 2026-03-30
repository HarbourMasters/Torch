#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <vector>
#include <string>
#include <optional>

namespace OoT {

// A single scene/room command as parsed from ROM
struct SceneCommand {
    uint32_t cmdID;
    std::vector<uint8_t> data; // serialized binary data for the command body
};

// Parsed scene/room data
class OoTSceneData : public IParsedData {
public:
    std::vector<SceneCommand> commands;
};

class OoTSceneBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTSceneFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTSceneBinaryExporter)
        };
    }
};

// Standalone cutscene factory for YAML-declared OOT:CUTSCENE assets.
// Uses the same re-serialization as the scene factory's SetCutscenes handler.
class OoTCutsceneBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTCutsceneFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTCutsceneBinaryExporter)
        };
    }
};

} // namespace OoT

#endif
