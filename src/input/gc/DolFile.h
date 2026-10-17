#pragma once

#include <cstdint>
#include <vector>

namespace GC {

struct DolSection {
    bool text;
    uint32_t fileOffset;
    uint32_t address;
    uint32_t size;
};

class DolFile {
public:
    explicit DolFile(const std::vector<uint8_t>& data);

    const std::vector<DolSection>& GetSections() const { return mSections; }
    uint32_t GetBssAddress() const { return mBssAddress; }
    uint32_t GetBssSize() const { return mBssSize; }
    uint32_t GetEntryPoint() const { return mEntry; }

    uint32_t GetStart() const;
    uint32_t GetEnd() const;

    void Map(uint8_t* dst, uint32_t base, uint32_t size) const;

private:
    std::vector<uint8_t> mData;
    std::vector<DolSection> mSections;
    uint32_t mBssAddress;
    uint32_t mBssSize;
    uint32_t mEntry;
};

} // namespace GC
