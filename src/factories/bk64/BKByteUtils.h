#pragma once

#include <cstdint>
#include <vector>

namespace BK64 {

// Big-endian scalar reads from raw ROM/overlay bytes.
inline uint16_t ReadU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline int16_t ReadS16BE(const uint8_t* p) {
    return static_cast<int16_t>(ReadU16BE(p));
}

inline uint32_t ReadU32BE(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

inline int32_t ReadS32BE(const uint8_t* p) {
    return static_cast<int32_t>(ReadU32BE(p));
}

inline void WriteU32BE(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>(value >> 16);
    p[2] = static_cast<uint8_t>(value >> 8);
    p[3] = static_cast<uint8_t>(value);
}

inline void PushBE(std::vector<uint8_t>& out, uint32_t value, int bytes) {
    for (int i = bytes - 1; i >= 0; i--) {
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

} // namespace BK64
