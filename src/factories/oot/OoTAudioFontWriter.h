#pragma once

#ifdef OOT_SUPPORT

#include "OoTAudioTypes.h"

namespace OoT {

struct DrumEntry {
    uint8_t releaseRate, pan, loaded;
    float tuning;
    std::vector<std::pair<int16_t, int16_t>> env;
    std::string sampleRef;
};

struct InstEntry {
    bool isValid;
    uint8_t loaded, normalRangeLo, normalRangeHi, releaseRate;
    std::vector<std::pair<int16_t, int16_t>> env;
    uint32_t lowAddr, normalAddr, highAddr;
};

// Cross-font stack residue state. ZAPDTR reuses the same stack frame
// across fonts, so invalid instruments inherit field values from the
// previous font's last valid instrument.
struct FontResidueState {
    uint8_t loaded = 0, rangeLo = 0, rangeHi = 0, release = 0;
};

struct SFXEntry {
    bool exists;
    std::string sampleRef;
    float tuning;
};

class AudioFontWriter {
public:
    void Extract(YAML::Node& node,
                 SafeAudioBankReader& audioBank,
                 const std::vector<AudioTableEntry>& fontTable,
                 const std::vector<AudioTableEntry>& sampleBankTable,
                 std::map<uint32_t, SampleInfo>& sampleMap);

private:
    std::string GetSampleRef(int bankIndex, uint32_t sampleAddr, uint32_t baseOffset,
                             SafeAudioBankReader& audioBank,
                             const std::vector<AudioTableEntry>& sampleBankTable,
                             std::map<uint32_t, SampleInfo>& sampleMap);
    std::vector<std::pair<int16_t, int16_t>> ParseEnvelope(uint32_t envOffset, SafeAudioBankReader& audioBank);
    void WriteSFE(LUS::BinaryWriter& w, uint32_t sfeOffset, uint32_t baseOffset,
                  int bankIndex, SafeAudioBankReader& audioBank,
                  const std::vector<AudioTableEntry>& sampleBankTable,
                  std::map<uint32_t, SampleInfo>& sampleMap);
    void WriteEnvData(LUS::BinaryWriter& w, const std::vector<std::pair<int16_t, int16_t>>& envs);
    std::vector<InstEntry> ParseInstruments(int numInstruments, uint32_t ptr,
                                          const std::vector<DrumEntry>& drums,
                                          SafeAudioBankReader& audioBank,
                                          FontResidueState& residue);
    std::vector<SFXEntry> ParseSFX(int numSfx, uint32_t ptr, int sampleBankId,
                                    SafeAudioBankReader& audioBank,
                                    const std::vector<AudioTableEntry>& sampleBankTable,
                                    std::map<uint32_t, SampleInfo>& sampleMap);
    std::vector<DrumEntry> ParseDrums(int numDrums, uint32_t ptr, int sampleBankId,
                                      SafeAudioBankReader& audioBank,
                                      const std::vector<AudioTableEntry>& sampleBankTable,
                                      std::map<uint32_t, SampleInfo>& sampleMap);
};

} // namespace OoT

#endif
