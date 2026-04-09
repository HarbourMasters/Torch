#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <map>

namespace OoT {

// Safe big-endian random-access reader over audio bank data.
// Bounds-checks each read and returns 0 on out-of-range (avoids raw pointer arithmetic).
class SafeAudioBankReader {
public:
    SafeAudioBankReader(std::vector<uint8_t> data);
    uint8_t ReadU8(uint32_t offset);
    uint32_t ReadU32(uint32_t offset);
    int16_t ReadS16(uint32_t offset);
    float ReadFloat(uint32_t offset);
    size_t Size() const { return mData.size(); }

private:
    std::vector<uint8_t> mData;
    LUS::BinaryReader mReader;
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

struct AudioParseContext {
    SafeAudioBankReader& audioBank;
    const std::vector<AudioTableEntry>& sampleBankTable;
    std::map<int, std::map<int, std::string>> sampleNames;
    std::map<uint32_t, SampleInfo>& sampleMap;
};

} // namespace OoT

#endif
