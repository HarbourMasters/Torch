#pragma once

#ifdef OOT_SUPPORT

#include "OoTAudioTypes.h"

namespace OoT {

class AudioSequenceWriter {
public:
    bool Extract(std::vector<uint8_t>& buffer, YAML::Node& node,
                 const std::vector<AudioTableEntry>& seqTable,
                 const std::vector<std::vector<uint8_t>>& seqFontMap);

private:
    void WriteCompanion(const uint8_t* seqData, uint32_t seqSize,
                        uint32_t originalIndex, uint8_t medium, uint8_t cachePolicy,
                        const std::vector<uint8_t>& fonts, const std::string& seqName);
};

} // namespace OoT

#endif
