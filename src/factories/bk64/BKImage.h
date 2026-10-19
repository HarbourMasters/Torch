#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace BK64 {

// 8-bit RGBA, rows top to bottom.
struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

// A channel of `bits` bits to 8 and back. Each undoes the other.
inline uint8_t ExpandBits(uint32_t value, int bits) {
    const uint32_t max = (1u << bits) - 1;
    return (uint8_t)((value * 255 + max / 2) / max);
}

inline uint32_t CompressBits(uint8_t value, int bits) {
    const uint32_t max = (1u << bits) - 1;
    return (value * max + 127) / 255;
}

struct IndexedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> indices;
    std::vector<uint8_t> palette; // RGBA per entry
};

// A PNG holds one frame and a GIF one per frame, each the full canvas. Delays are in ms.
bool DecodeImage(const std::vector<uint8_t>& file, std::vector<RgbaImage>& frames, std::vector<int>* delays,
                 std::string& error);
// A palette PNG's own indices and palette. False for any other PNG.
bool DecodeIndexedPng(const std::vector<uint8_t>& file, IndexedImage& out);
bool IsGif(const std::vector<uint8_t>& file);

std::vector<uint8_t> EncodePng(const RgbaImage& image);
// One channel, or two with alpha.
std::vector<uint8_t> EncodeGrayPng(int width, int height, const std::vector<uint8_t>& channels, bool alpha);
std::vector<uint8_t> EncodeIndexedPng(const IndexedImage& image);

// Up to `colors` entries, all exact RGBA5551. Pixels under half alpha share one transparent entry.
IndexedImage Quantize(const RgbaImage& image, int colors);

} // namespace BK64
