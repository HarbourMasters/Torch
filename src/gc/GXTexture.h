#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace GX {

enum class TexFormat {
    I4 = 0x0,
    I8 = 0x1,
    IA4 = 0x2,
    IA8 = 0x3,
    RGB565 = 0x4,
    RGB5A3 = 0x5,
    RGBA8 = 0x6,
    C4 = 0x8,
    C8 = 0x9,
    C14X2 = 0xA,
    CMPR = 0xE,
};

struct BlockInfo {
    uint32_t width;
    uint32_t height;
    uint32_t bitsPerTexel;
};

BlockInfo GetBlockInfo(TexFormat format);

size_t TiledSize(TexFormat format, uint32_t width, uint32_t height);

std::vector<uint8_t> Detile(TexFormat format, const uint8_t* src, size_t size, uint32_t width, uint32_t height);

std::vector<uint8_t> DecodeRGBA8(TexFormat format, const uint8_t* src, size_t size, uint32_t width, uint32_t height,
                                 const uint8_t* tlut = nullptr, size_t tlutEntries = 0);

void Rgb5a3ToRgba8(uint16_t texel, uint8_t* out);
uint16_t Rgb5a3ToRgba5551(uint16_t texel);
bool Rgb5a3IsLossless(uint16_t texel);
bool Rgb5a3HasPartialAlpha(uint16_t texel);

enum class N64Format { CI4, CI8, I4, I8, IA8, IA16, RGBA16, RGBA32 };

std::optional<N64Format> ParseN64Format(const std::string& name);
std::optional<TexFormat> SourceFormatFor(N64Format format);

struct N64Texture {
    N64Format format;
    std::vector<uint8_t> data;
};

N64Texture ConvertToN64(N64Format format, const uint8_t* src, size_t size, uint32_t width, uint32_t height);

bool Rgb5a3TextureIsLossless(const uint8_t* src, size_t size, uint32_t width, uint32_t height);

struct N64Tlut {
    std::vector<uint8_t> data;
    uint32_t partialAlpha;
};

N64Tlut ConvertTlut(const uint8_t* src, uint32_t count);

std::vector<uint8_t> ConvertTlutRgba8(const uint8_t* src, uint32_t count);

class ConversionStats {
public:
    void RecordTlut(uint32_t addr, uint32_t partialAlpha);
    void RecordPromotion(uint32_t addr, const std::string& name);
    uint32_t GetPartialAlphaEntries() const;
    uint32_t GetPartialAlphaTluts() const;
    std::map<uint32_t, std::string> GetPromotions() const;
    void Clear();

private:
    mutable std::mutex mMutex;
    std::map<uint32_t, uint32_t> mTluts;
    std::map<uint32_t, std::string> mPromotions;
};

ConversionStats& GetStats();

} // namespace GX
