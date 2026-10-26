#include "DolFile.h"
#include "BlockReader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace GC {

DolFile::DolFile(const std::vector<uint8_t>& data) : mData(data) {
    if (data.size() < 0x100) {
        throw std::runtime_error("DOL: file too small");
    }
    const uint8_t* h = data.data();
    for (int i = 0; i < 18; i++) {
        const bool text = i < 7;
        const uint32_t offset = ReadBE32(h + i * 4);
        const uint32_t address = ReadBE32(h + 0x48 + i * 4);
        const uint32_t size = ReadBE32(h + 0x90 + i * 4);
        if (size == 0) {
            continue;
        }
        if (static_cast<uint64_t>(offset) + size > data.size()) {
            throw std::runtime_error("DOL: section exceeds file size");
        }
        mSections.push_back({ text, offset, address, size });
    }
    mBssAddress = ReadBE32(h + 0xD8);
    mBssSize = ReadBE32(h + 0xDC);
    mEntry = ReadBE32(h + 0xE0);
}

uint32_t DolFile::GetStart() const {
    uint32_t start = UINT32_MAX;
    for (const auto& s : mSections) {
        start = std::min(start, s.address);
    }
    if (mBssSize != 0) {
        start = std::min(start, mBssAddress);
    }
    return start;
}

uint32_t DolFile::GetEnd() const {
    uint32_t end = 0;
    for (const auto& s : mSections) {
        end = std::max(end, s.address + s.size);
    }
    if (mBssSize != 0) {
        end = std::max(end, mBssAddress + mBssSize);
    }
    return end;
}

void DolFile::Map(uint8_t* dst, uint32_t base, uint32_t size) const {
    for (const auto& s : mSections) {
        if (s.address < base || static_cast<uint64_t>(s.address - base) + s.size > size) {
            throw std::runtime_error("DOL: section outside its window");
        }
        std::memcpy(dst + (s.address - base), mData.data() + s.fileOffset, s.size);
    }
}

} // namespace GC
