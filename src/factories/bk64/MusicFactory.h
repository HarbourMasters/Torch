#pragma once

#include "factories/BaseFactory.h"
#include "types/RawBuffer.h"

#include <cstdint>
#include <vector>

namespace BK64 {

constexpr uint32_t kDefaultMusicVolume = 32767;

enum class MusicEventKind : uint8_t {
    Midi = 0,
    Tempo = 1,
    LoopStart = 2,
    LoopEnd = 3,
    End = 4,
};

struct MusicEvent {
    uint32_t delta = 0;
    uint8_t kind = 0;
    uint8_t status = 0;
    uint8_t byte1 = 0;
    uint8_t byte2 = 0;
    uint32_t aux = 0;
};

struct MusicTrack {
    uint32_t index = 0;
    bool present = false;
    std::vector<MusicEvent> events;
};

class MusicData : public RawBuffer {
  public:
    uint32_t mDivision = 0;
    uint32_t mVolume = kDefaultMusicVolume;
    std::vector<MusicTrack> mTracks;

    explicit MusicData(std::vector<uint8_t> bytes) : RawBuffer(bytes) {
    }
};

class MusicBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MusicModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MusicFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, MusicBinaryExporter) REGISTER(Modding, MusicModdingExporter) };
    }
    bool SupportModdedAssets() override {
        return true;
    }
};

} // namespace BK64
