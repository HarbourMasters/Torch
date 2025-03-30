#pragma once

#include <factories/BaseFactory.h>

namespace FZX {

typedef struct GhostMachineInfo {
    /* 0x00 */ uint8_t character;
    /* 0x01 */ uint8_t customType;
    /* 0x02 */ uint8_t frontType;
    /* 0x03 */ uint8_t rearType;
    /* 0x04 */ uint8_t wingType;
    /* 0x05 */ uint8_t logo;
    /* 0x06 */ uint8_t number;
    /* 0x07 */ uint8_t decal;
    /* 0x08 */ uint8_t bodyR;
    /* 0x09 */ uint8_t bodyG;
    /* 0x0A */ uint8_t bodyB;
    /* 0x0B */ uint8_t numberR;
    /* 0x0C */ uint8_t numberG;
    /* 0x0D */ uint8_t numberB;
    /* 0x0E */ uint8_t decalR;
    /* 0x0F */ uint8_t decalG;
    /* 0x10 */ uint8_t decalB;
    /* 0x11 */ uint8_t cockpitR;
    /* 0x12 */ uint8_t cockpitG;
    /* 0x13 */ uint8_t cockpitB;
} GhostMachineInfo;

class GhostRecordData : public IParsedData {
public:
    uint16_t mGhostType;
    int32_t mReplayChecksum;
    int32_t mCourseEncoding;
    int32_t mRaceTime;
    std::string mTrackName; // empty for normal tracks
    GhostMachineInfo mGhostMachineInfo;

    GhostRecordData(uint16_t ghostType, int32_t replayChecksum, int32_t courseEncoding, int32_t raceTime, std::string trackName, GhostMachineInfo& ghostMachineInfo) : mGhostType(ghostType), mReplayChecksum(replayChecksum), mCourseEncoding(courseEncoding), mRaceTime(raceTime), mTrackName(trackName), mGhostMachineInfo(ghostMachineInfo) {}
};

class GhostRecordHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GhostRecordBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GhostRecordCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GhostRecordModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GhostRecordFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, GhostRecordCodeExporter)
            REGISTER(Header, GhostRecordHeaderExporter)
            REGISTER(Binary, GhostRecordBinaryExporter)
            REGISTER(Modding, GhostRecordModdingExporter)
        };
    }
    bool SupportModdedAssets() override { return true; }
};
} // namespace FZX
