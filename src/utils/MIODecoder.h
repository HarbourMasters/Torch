#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>

class MIO0Decoder {
public:
    static std::unordered_map<uint32_t, std::vector<uint8_t>> gCachedChunks;
    static std::vector<uint8_t>& Decode(std::vector<uint8_t>& buffer, uint32_t offset);
    static void ClearCache();
};
