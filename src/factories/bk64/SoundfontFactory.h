#pragma once

#include "factories/BaseFactory.h"
#include "types/RawBuffer.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace BK64 {

struct SoundfontBook {
    int32_t order = 0;
    int32_t npredictors = 0;
    std::vector<int16_t> book;
};

struct SoundfontLoop {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t count = 0;
    std::vector<int16_t> state;
};

struct SoundfontEnvelope {
    int32_t attackTime = 0;
    int32_t decayTime = 0;
    int32_t releaseTime = 0;
    uint8_t attackVolume = 0;
    uint8_t decayVolume = 0;
};

struct SoundfontKeyMap {
    uint8_t velocityMin = 0;
    uint8_t velocityMax = 0;
    uint8_t keyMin = 0;
    uint8_t keyMax = 0;
    uint8_t keyBase = 0;
    int8_t detune = 0;
};

struct SoundfontWave {
    uint32_t base = 0;
    int32_t len = 0;
    uint8_t type = 0;
    uint8_t flags = 0;
    uint32_t loopOffset = 0;
    uint32_t bookOffset = 0;
};

struct SoundfontSound {
    uint32_t envelopeOffset = 0;
    uint32_t keyMapOffset = 0;
    uint32_t waveOffset = 0;
    uint8_t samplePan = 0;
    uint8_t sampleVolume = 0;
    uint8_t flags = 0;
};

struct SoundfontInstrument {
    uint8_t volume = 0;
    uint8_t pan = 0;
    uint8_t priority = 0;
    uint8_t flags = 0;
    uint8_t tremType = 0;
    uint8_t tremRate = 0;
    uint8_t tremDepth = 0;
    uint8_t tremDelay = 0;
    uint8_t vibType = 0;
    uint8_t vibRate = 0;
    uint8_t vibDepth = 0;
    uint8_t vibDelay = 0;
    int16_t bendRange = 0;
    std::vector<uint32_t> soundOffsets;
};

struct SoundfontBank {
    uint8_t flags = 0;
    uint8_t pad = 0;
    int32_t sampleRate = 0;
    uint32_t percussionOffset = 0;
    std::vector<uint32_t> instrumentOffsets;
};

struct SoundfontImage {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> covered;
};

class SoundfontData : public RawBuffer {
  public:
    int16_t mRevision = 0;
    std::vector<uint32_t> mBankOffsets;
    std::map<uint32_t, SoundfontBank> mBanks;
    std::map<uint32_t, SoundfontInstrument> mInstruments;
    std::map<uint32_t, SoundfontSound> mSounds;
    std::map<uint32_t, SoundfontEnvelope> mEnvelopes;
    std::map<uint32_t, SoundfontKeyMap> mKeyMaps;
    std::map<uint32_t, SoundfontWave> mWaves;
    std::map<uint32_t, SoundfontBook> mBooks;
    std::map<uint32_t, SoundfontLoop> mLoops;
    std::vector<uint8_t> mSampleData;

    explicit SoundfontData(std::vector<uint8_t> bytes) : RawBuffer(bytes) {
    }

    uint64_t SampleDataEnd() const;
    SoundfontImage Serialize() const;
    std::vector<uint8_t> SampleBytes(const SoundfontWave& wave) const;
};

struct SoundfontModFile {
    std::string yaml;
    std::vector<uint8_t> raw;
    std::vector<uint8_t> wav;
};

SoundfontData ParseSoundfont(const uint8_t* ctl, size_t ctlSize);

class SoundfontBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class SoundfontModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class SoundfontFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, SoundfontBinaryExporter) REGISTER(Modding, SoundfontModdingExporter) };
    }
    bool SupportModdedAssets() override {
        return true;
    }
};

uint32_t LocateSoundfontCtl(const std::vector<uint8_t>& rom, uint32_t ctlOffset, uint32_t ctlSize);

} // namespace BK64
