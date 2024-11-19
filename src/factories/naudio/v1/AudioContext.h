#pragma once

#include "AudioTableFactory.h"
#include "factories/naudio/v0/AudioHeaderFactory.h"
#include "factories/BaseFactory.h"

struct TunedSample {
    uint32_t sample;
    uint32_t sampleBankId;
    float tuning;
};

class AudioContext {
public:
    static std::unordered_map<AudioTableType, std::unordered_map<uint32_t, AudioTableEntry>> tables;
    static std::unordered_map<AudioTableType, std::shared_ptr<AudioTableData>> tableData;
    static std::unordered_map<AudioTableType, std::vector<uint8_t>> data;

    static LUS::BinaryReader MakeReader(AudioTableType type, uint32_t offset);
    static TunedSample LoadTunedSample(LUS::BinaryReader& reader, uint32_t parent, uint32_t sampleBankId);
    static uint64_t GetPathByAddr(uint32_t addr);
};

class AudioContextFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, AudioDummyExporter)
            REGISTER(Binary, AudioDummyExporter)
            REGISTER(Code, AudioDummyExporter)
        };
    }
};
