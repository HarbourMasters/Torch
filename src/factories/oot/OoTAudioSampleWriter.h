#pragma once

#ifdef OOT_SUPPORT

#include "OoTAudioTypes.h"

namespace OoT {

class AudioSampleWriter {
public:
    bool Extract(std::vector<uint8_t>& buffer, YAML::Node& node,
                 SafeAudioBankReader& audioBank,
                 const std::vector<AudioTableEntry>& fontTable,
                 const std::vector<AudioTableEntry>& sampleBankTable,
                 std::map<uint32_t, SampleInfo>& sampleMap);

private:
    void DiscoverSamples(const std::vector<AudioTableEntry>& fontTable, AudioParseContext& ctx);
    void WriteCompanionFiles(const std::map<uint32_t, SampleInfo>& sampleMap,
                             const std::vector<uint8_t>& audioTable);
    std::map<int, std::map<int, std::string>> ParseSampleNames(YAML::Node& node);
    void ParseSample(int bankIndex, uint32_t sampleAddr, uint32_t baseOffset, AudioParseContext& ctx);
    void ParseSFESample(int bankIndex, uint32_t sfeAddr, uint32_t baseOffset, AudioParseContext& ctx);
};

} // namespace OoT

#endif
