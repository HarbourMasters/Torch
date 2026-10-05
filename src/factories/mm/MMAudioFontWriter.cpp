#include "MMAudioFontWriter.h"

#include "Companion.h"

#include <cstdint>

namespace MM {
namespace {

uint32_t ReadU32(const std::vector<char>& data, size_t offset) {
    return static_cast<uint8_t>(data[offset]) | (static_cast<uint8_t>(data[offset + 1]) << 8) |
           (static_cast<uint8_t>(data[offset + 2]) << 16) | (static_cast<uint8_t>(data[offset + 3]) << 24);
}

bool SkipSFE(const std::vector<char>& data, size_t& offset) {
    if (offset >= data.size()) {
        return false;
    }
    if (data[offset++] == 0) {
        return true;
    }
    if (offset + 5 > data.size()) {
        return false;
    }
    ++offset;
    const uint32_t nameLength = ReadU32(data, offset);
    offset += 4;
    if (offset + nameLength + 4 > data.size()) {
        return false;
    }
    offset += nameLength + 4;
    return true;
}

bool SkipDrumSample(const std::vector<char>& data, size_t& offset) {
    if (offset + 5 > data.size()) {
        return false;
    }
    ++offset;
    const uint32_t nameLength = ReadU32(data, offset);
    offset += 4;
    if (offset + nameLength + 4 > data.size()) {
        return false;
    }
    offset += nameLength + 4;
    return true;
}

void NormalizeNullInstrumentData(std::vector<char>& data) {
    constexpr size_t kHeaderSize = 0x40;
    constexpr uint32_t kSoundfontType = 0x4F534654; // OSFT
    if (data.size() < kHeaderSize + 24 || ReadU32(data, 4) != kSoundfontType) {
        return;
    }

    const uint32_t drumCount = ReadU32(data, kHeaderSize + 12);
    const uint32_t instrumentCount = ReadU32(data, kHeaderSize + 16);
    size_t offset = kHeaderSize + 24;

    for (uint32_t i = 0; i < drumCount; ++i) {
        if (offset + 7 > data.size()) {
            return;
        }
        offset += 3;
        const uint32_t envCount = ReadU32(data, offset);
        offset += 4;
        if (offset + static_cast<size_t>(envCount) * 4 > data.size()) {
            return;
        }
        offset += static_cast<size_t>(envCount) * 4;
        if (!SkipDrumSample(data, offset)) {
            return;
        }
    }

    for (uint32_t i = 0; i < instrumentCount; ++i) {
        const size_t entryOffset = offset;
        if (offset + 9 > data.size()) {
            return;
        }
        const bool valid = data[offset++] != 0;
        offset += 4;
        const uint32_t envCount = ReadU32(data, offset);
        offset += 4;
        if (offset + static_cast<size_t>(envCount) * 4 > data.size()) {
            return;
        }
        offset += static_cast<size_t>(envCount) * 4;
        if (!valid && static_cast<uint8_t>(data[entryOffset + 1]) == 0x4A) {
            data[entryOffset + 1] = 0;
        }
        for (int sample = 0; sample < 3; ++sample) {
            if (!SkipSFE(data, offset)) {
                return;
            }
        }
    }
}

} // namespace

void MMAudioFontWriter::NormalizeNullInstruments() {
    Companion::Instance->PatchCompanionFiles(
        [](const std::string& path) { return path.starts_with("fonts/"); },
        [](std::vector<char>& data) { NormalizeNullInstrumentData(data); });
}

} // namespace MM
