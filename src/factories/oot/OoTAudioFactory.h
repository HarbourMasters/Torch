#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"

namespace OoT {

class OoTAudioData : public IParsedData {
public:
    std::vector<char> mMainEntry; // The 64-byte OAUD header (empty body)
    // Future: companion file data for samples, fonts, sequences
};

class OoTAudioBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

// Audio table entry (16 bytes each in ROM)
struct AudioTableEntry {
    uint32_t ptr;
    uint32_t size;
    uint8_t medium;
    uint8_t cachePolicy;
    int16_t data1;
    int16_t data2;
    int16_t data3;
};

class OoTAudioFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTAudioBinaryExporter)
        };
    }

private:
    std::vector<char> BuildMainAudioHeader();
    bool ExtractSequences(std::vector<uint8_t>& buffer, YAML::Node& node,
                          const std::vector<AudioTableEntry>& seqTable,
                          const std::vector<std::vector<uint8_t>>& seqFontMap);
    void WriteSequenceCompanion(const uint8_t* seqData, uint32_t seqSize,
                                uint32_t originalIndex, uint8_t medium, uint8_t cachePolicy,
                                const std::vector<uint8_t>& fonts, const std::string& seqName);
    std::vector<AudioTableEntry> ParseAudioTable(const uint8_t* codeData, uint32_t tableOffset);
    std::vector<std::vector<uint8_t>> ParseSequenceFontTable(const uint8_t* codeData,
                                                              uint32_t tableOffset, uint32_t numSequences);
};

} // namespace OoT

#endif
