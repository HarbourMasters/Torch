#pragma once

#ifdef OOT_SUPPORT

#include "OoTAudioTypes.h"

namespace OoT {

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
};

} // namespace OoT

#endif
