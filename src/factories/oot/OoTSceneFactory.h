#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <vector>
#include <string>
#include <optional>
#include <set>

namespace OoT {

// Scene command IDs (matching OoT's RoomCommand enum)
enum SceneCmdID : uint32_t {
    SetStartPositionList = 0x00,
    SetActorList = 0x01,
    SetCsCamera = 0x02,
    SetCollisionHeader = 0x03,
    SetRoomList = 0x04,
    SetWind = 0x05,
    SetEntranceList = 0x06,
    SetSpecialObjects = 0x07,
    SetRoomBehavior = 0x08,
    // 0x09 = Unused
    SetMesh = 0x0A,
    SetObjectList = 0x0B,
    SetLightList = 0x0C,
    SetPathways = 0x0D,
    SetTransitionActorList = 0x0E,
    SetLightingSettings = 0x0F,
    SetTimeSettings = 0x10,
    SetSkyboxSettings = 0x11,
    SetSkyboxModifier = 0x12,
    SetExitList = 0x13,
    EndMarker = 0x14,
    SetSoundSettings = 0x15,
    SetEchoSettings = 0x16,
    SetCutscenes = 0x17,
    SetAlternateHeaders = 0x18,
    SetCameraSettings = 0x19,
};

// Deferred alternate header entry, processed after primary commands.
struct PendingAltHeader {
    uint32_t seg;
    std::string symbol;
};

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

private:
    void CreateBackgroundCompanion(std::vector<uint8_t>& buffer, uint32_t source,
                                   const std::string& bgSymbol);
    std::string ResolveGfxPointer(uint32_t ptr, const std::string& symbol);
    std::string ResolveGfxWithAlias(uint32_t ptr, const std::string& symbol,
                                    const std::string& currentDir);
    std::set<uint32_t> CollectKnownAddresses(const std::vector<std::pair<uint32_t, uint32_t>>& rawCmds,
                                              std::vector<uint8_t>& buffer);
    uint32_t GetNeighborSize(const std::set<uint32_t>& knownAddrs, uint32_t segAddr, uint32_t entrySize);
};

} // namespace OoT

#endif
