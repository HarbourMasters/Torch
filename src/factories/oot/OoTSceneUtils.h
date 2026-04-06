#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <sstream>
#include <iomanip>

namespace OoT {

// Cutscene field packing macros (matching OTRExporter's command_macros_base.h)
inline uint32_t CS_CMD_HH(uint16_t a, uint16_t b) { return ((uint32_t)b << 16) | (uint32_t)a; }
inline uint32_t CS_CMD_BBH(int8_t a, int8_t b, int16_t c) {
    return ((uint32_t)(uint8_t)a) | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint16_t)c << 16);
}
inline uint32_t CS_CMD_HBB(uint16_t a, uint8_t b, uint8_t c) {
    return (uint32_t)a | ((uint32_t)b << 16) | ((uint32_t)c << 24);
}

// Helper to read a sub-array from ROM given a segmented pointer
LUS::BinaryReader ReadSubArray(std::vector<uint8_t>& buffer, uint32_t segAddr, uint32_t size);

// Resolve a segmented pointer to an O2R asset path string
std::string ResolvePointer(uint32_t ptr);

// Build a scene-relative asset name from offset
std::string MakeAssetName(const std::string& baseName, const std::string& suffix, uint32_t offset);

// Serialize pathway data into OoTPath binary format.
std::vector<char> SerializePathways(std::vector<uint8_t>& buffer,
                                    const std::vector<std::pair<uint8_t, uint32_t>>& pathways,
                                    uint32_t writeCount, uint32_t repeats);

class CutsceneSerializer {
public:
    static std::vector<char> Serialize(std::vector<uint8_t>& buffer, uint32_t segAddr);
private:
    static uint32_t CalculateSize(std::vector<uint8_t>& buffer, uint32_t segAddr);
    static std::vector<char> Write(std::vector<uint8_t>& buffer, uint32_t segAddr, uint32_t size);
};

} // namespace OoT

#endif
