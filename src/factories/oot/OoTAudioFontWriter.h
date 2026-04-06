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

// Cross-font stack residue. ZAPDTR reuses the same stack frame across fonts,
// so invalid instruments inherit field values from the previous font's last
// valid instrument. See docs/oot-audio-font-residue-analysis.md
class FontResidue {
public:
    void Reset();
    void SeedFromDrums(const std::vector<DrumEntry>& drums);
    void ApplyToInstrument(InstEntry& inst) const;
    void UpdateFromInstrument(const InstEntry& inst);
private:
    uint8_t mLoaded = 0, mRangeLo = 0, mRangeHi = 0, mRelease = 0;
};

struct SFXEntry {
    bool exists;
    std::string sampleRef;
    float tuning;
};

struct FontWriteContext {
    SafeAudioBankReader& audioBank;
    const std::vector<AudioTableEntry>& sampleBankTable;
    std::map<uint32_t, SampleInfo>& sampleMap;
};

class AudioFontWriter {
public:
    void Extract(YAML::Node& node,
                 SafeAudioBankReader& audioBank,
                 const std::vector<AudioTableEntry>& fontTable,
                 const std::vector<AudioTableEntry>& sampleBankTable,
                 std::map<uint32_t, SampleInfo>& sampleMap);

private:
    std::string GetSampleRef(int bankIndex, uint32_t sampleAddr, uint32_t baseOffset, FontWriteContext& ctx);
    std::vector<std::pair<int16_t, int16_t>> ParseEnvelope(uint32_t envOffset, FontWriteContext& ctx);
    void WriteSFE(LUS::BinaryWriter& w, uint32_t sfeOffset, uint32_t baseOffset,
                  int bankIndex, FontWriteContext& ctx);
    void WriteFontCompanion(uint32_t fontIndex, const AudioTableEntry& fontEntry,
                            const std::vector<DrumEntry>& drums,
                            const std::vector<InstEntry>& instruments,
                            const std::vector<SFXEntry>& sfxEntries,
                            uint32_t ptr, int sampleBankId,
                            FontWriteContext& ctx,
                            const std::map<int, std::string>& fontNames);
    void WriteDrums(LUS::BinaryWriter& w, const std::vector<DrumEntry>& drums);
    void WriteInstruments(LUS::BinaryWriter& w, const std::vector<InstEntry>& instruments,
                          uint32_t ptr, int sampleBankId, FontWriteContext& ctx);
    void WriteSFXEntries(LUS::BinaryWriter& w, const std::vector<SFXEntry>& sfxEntries);
    void WriteEnvData(LUS::BinaryWriter& w, const std::vector<std::pair<int16_t, int16_t>>& envs);
    std::vector<InstEntry> ParseInstruments(int numInstruments, uint32_t ptr, FontWriteContext& ctx);
    std::vector<SFXEntry> ParseSFX(int numSfx, uint32_t ptr, int sampleBankId, FontWriteContext& ctx);
    std::vector<DrumEntry> ParseDrums(int numDrums, uint32_t ptr, int sampleBankId, FontWriteContext& ctx);

    FontResidue mResidue;
};

} // namespace OoT

#endif
