#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <map>

namespace OoT {

// Safe big-endian random-access reader over audio bank data.
// Bounds-checks each read and returns 0 on out-of-range (avoids raw pointer arithmetic).
class SafeAudioBankReader {
public:
    SafeAudioBankReader(const std::vector<uint8_t>& data);
    uint32_t ReadU32(uint32_t offset);
    int16_t ReadS16(uint32_t offset);
    float ReadFloat(uint32_t offset);

private:
    const std::vector<uint8_t>& mData;
    LUS::BinaryReader mReader;
};

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

struct SampleInfo {
    uint8_t codec, medium, unk_bit26, unk_bit25;
    uint32_t dataSize, dataOffset;
    int32_t loopStart, loopEnd, loopCount;
    std::vector<int16_t> loopStates;
    int32_t bookOrder, bookNpredictors;
    std::vector<int16_t> books;
    std::string name;
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
    bool ExtractSamples(std::vector<uint8_t>& buffer, YAML::Node& node,
                        std::vector<uint8_t>& audioBankData, SafeAudioBankReader& audioBank,
                        const std::vector<AudioTableEntry>& fontTable,
                        const std::vector<AudioTableEntry>& sampleBankTable,
                        std::map<uint32_t, SampleInfo>& sampleMap);
    void ExtractFonts(YAML::Node& node,
                      std::vector<uint8_t>& audioBankData, SafeAudioBankReader& audioBank,
                      const std::vector<AudioTableEntry>& fontTable,
                      const std::vector<AudioTableEntry>& sampleBankTable,
                      std::map<uint32_t, SampleInfo>& sampleMap);
    void WriteSequenceCompanion(const uint8_t* seqData, uint32_t seqSize,
                                uint32_t originalIndex, uint8_t medium, uint8_t cachePolicy,
                                const std::vector<uint8_t>& fonts, const std::string& seqName);
    std::vector<AudioTableEntry> ParseAudioTable(const uint8_t* codeData, uint32_t tableOffset);
    std::vector<std::vector<uint8_t>> ParseSequenceFontTable(const uint8_t* codeData,
                                                              uint32_t tableOffset, uint32_t numSequences);
};

} // namespace OoT

#endif
