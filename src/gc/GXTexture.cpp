#include "GXTexture.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace GX {

BlockInfo GetBlockInfo(TexFormat format) {
    switch (format) {
        case TexFormat::I4:
        case TexFormat::C4:
        case TexFormat::CMPR:
            return { 8, 8, 4 };
        case TexFormat::I8:
        case TexFormat::IA4:
        case TexFormat::C8:
            return { 8, 4, 8 };
        case TexFormat::IA8:
        case TexFormat::RGB565:
        case TexFormat::RGB5A3:
        case TexFormat::C14X2:
            return { 4, 4, 16 };
        case TexFormat::RGBA8:
            return { 4, 4, 32 };
    }
    throw std::runtime_error("Unknown GX texture format");
}

size_t TiledSize(TexFormat format, uint32_t width, uint32_t height) {
    const auto info = GetBlockInfo(format);
    const size_t w = (width + info.width - 1) / info.width * info.width;
    const size_t h = (height + info.height - 1) / info.height * info.height;
    return w * h * info.bitsPerTexel / 8;
}

static uint16_t Be16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t GetTexel(const uint8_t* data, size_t index, uint32_t bits) {
    switch (bits) {
        case 4:
            return (index & 1) ? (data[index / 2] & 0xF) : (data[index / 2] >> 4);
        case 8:
            return data[index];
        default:
            return Be16(data + index * 2);
    }
}

static void SetTexel(uint8_t* data, size_t index, uint32_t bits, uint32_t value) {
    switch (bits) {
        case 4:
            if (index & 1) {
                data[index / 2] = (data[index / 2] & 0xF0) | (value & 0xF);
            } else {
                data[index / 2] = (data[index / 2] & 0x0F) | ((value & 0xF) << 4);
            }
            break;
        case 8:
            data[index] = value;
            break;
        default:
            data[index * 2] = value >> 8;
            data[index * 2 + 1] = value;
            break;
    }
}

static void CheckSize(TexFormat format, size_t size, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("GX texture has no texels");
    }
    if (size < TiledSize(format, width, height)) {
        throw std::runtime_error("GX texture data is smaller than its tiled size");
    }
}

std::vector<uint8_t> Detile(TexFormat format, const uint8_t* src, size_t size, uint32_t width, uint32_t height) {
    if (format == TexFormat::CMPR) {
        throw std::runtime_error("CMPR has no texel form; decode it to RGBA8");
    }
    CheckSize(format, size, width, height);

    const auto info = GetBlockInfo(format);
    const uint32_t blocksX = (width + info.width - 1) / info.width;
    const uint32_t blocksY = (height + info.height - 1) / info.height;
    const uint32_t perBlock = info.width * info.height;

    if (format == TexFormat::RGBA8) {
        std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4);
        for (uint32_t by = 0; by < blocksY; by++) {
            for (uint32_t bx = 0; bx < blocksX; bx++) {
                const uint8_t* block = src + (static_cast<size_t>(by) * blocksX + bx) * 64;
                for (uint32_t i = 0; i < 16; i++) {
                    const uint32_t x = bx * 4 + i % 4;
                    const uint32_t y = by * 4 + i / 4;
                    if (x >= width || y >= height) {
                        continue;
                    }
                    uint8_t* d = out.data() + (static_cast<size_t>(y) * width + x) * 4;
                    d[0] = block[i * 2 + 1];
                    d[1] = block[32 + i * 2];
                    d[2] = block[32 + i * 2 + 1];
                    d[3] = block[i * 2];
                }
            }
        }
        return out;
    }

    std::vector<uint8_t> out((static_cast<size_t>(width) * height * info.bitsPerTexel + 7) / 8, 0);
    for (uint32_t by = 0; by < blocksY; by++) {
        for (uint32_t bx = 0; bx < blocksX; bx++) {
            const size_t base = (static_cast<size_t>(by) * blocksX + bx) * perBlock;
            for (uint32_t i = 0; i < perBlock; i++) {
                const uint32_t x = bx * info.width + i % info.width;
                const uint32_t y = by * info.height + i / info.width;
                if (x >= width || y >= height) {
                    continue;
                }
                SetTexel(out.data(), static_cast<size_t>(y) * width + x, info.bitsPerTexel,
                         GetTexel(src, base + i, info.bitsPerTexel));
            }
        }
    }
    return out;
}

static uint8_t Expand4(uint32_t v) {
    return (uint8_t)(v * 17);
}

static uint8_t Expand5(uint32_t v) {
    return (uint8_t)((v << 3) | (v >> 2));
}

static uint8_t Expand6(uint32_t v) {
    return (uint8_t)((v << 2) | (v >> 4));
}

static uint8_t Expand3(uint32_t v) {
    return (uint8_t)((v << 5) | (v << 2) | (v >> 1));
}

void Rgb5a3ToRgba8(uint16_t texel, uint8_t* out) {
    if (texel & 0x8000) {
        out[0] = Expand5((texel >> 10) & 0x1F);
        out[1] = Expand5((texel >> 5) & 0x1F);
        out[2] = Expand5(texel & 0x1F);
        out[3] = 0xFF;
    } else {
        out[0] = Expand4((texel >> 8) & 0xF);
        out[1] = Expand4((texel >> 4) & 0xF);
        out[2] = Expand4(texel & 0xF);
        out[3] = Expand3((texel >> 12) & 0x7);
    }
}

static uint32_t Nearest5From4(uint32_t v) {
    return (v * 17 * 31 + 127) / 255;
}

uint16_t Rgb5a3ToRgba5551(uint16_t texel) {
    if (texel & 0x8000) {
        return (uint16_t)((((texel >> 10) & 0x1F) << 11) | (((texel >> 5) & 0x1F) << 6) | ((texel & 0x1F) << 1) | 1);
    }
    const uint32_t r = Nearest5From4((texel >> 8) & 0xF);
    const uint32_t g = Nearest5From4((texel >> 4) & 0xF);
    const uint32_t b = Nearest5From4(texel & 0xF);
    const uint32_t a = ((texel >> 12) & 0x7) >= 4 ? 1 : 0;
    return (uint16_t)((r << 11) | (g << 6) | (b << 1) | a);
}

bool Rgb5a3IsLossless(uint16_t texel) {
    if (texel & 0x8000) {
        return true;
    }
    const uint32_t alpha = (texel >> 12) & 0x7;
    if (alpha == 0) {
        return true;
    }
    if (alpha != 7) {
        return false;
    }
    for (int shift = 0; shift <= 8; shift += 4) {
        const uint32_t c = (texel >> shift) & 0xF;
        if (c != 0 && c != 0xF) {
            return false;
        }
    }
    return true;
}

bool Rgb5a3HasPartialAlpha(uint16_t texel) {
    if (texel & 0x8000) {
        return false;
    }
    const uint32_t alpha = (texel >> 12) & 0x7;
    return alpha != 0 && alpha != 7;
}

static void DecodeCmprBlock(const uint8_t* src, uint8_t colors[16][4]) {
    const uint16_t c0 = Be16(src);
    const uint16_t c1 = Be16(src + 2);
    uint8_t palette[4][4];
    const uint8_t r0 = Expand5(c0 >> 11), g0 = Expand6((c0 >> 5) & 0x3F), b0 = Expand5(c0 & 0x1F);
    const uint8_t r1 = Expand5(c1 >> 11), g1 = Expand6((c1 >> 5) & 0x3F), b1 = Expand5(c1 & 0x1F);
    auto blend = [](uint32_t a, uint32_t b) { return (uint8_t)((a * 5 + b * 3) >> 3); };

    palette[0][0] = r0, palette[0][1] = g0, palette[0][2] = b0, palette[0][3] = 0xFF;
    palette[1][0] = r1, palette[1][1] = g1, palette[1][2] = b1, palette[1][3] = 0xFF;
    if (c0 > c1) {
        palette[2][0] = blend(r0, r1), palette[2][1] = blend(g0, g1), palette[2][2] = blend(b0, b1);
        palette[2][3] = 0xFF;
        palette[3][0] = blend(r1, r0), palette[3][1] = blend(g1, g0), palette[3][2] = blend(b1, b0);
        palette[3][3] = 0xFF;
    } else {
        palette[2][0] = (r0 + r1) / 2, palette[2][1] = (g0 + g1) / 2, palette[2][2] = (b0 + b1) / 2;
        palette[2][3] = 0xFF;
        palette[3][0] = palette[2][0], palette[3][1] = palette[2][1], palette[3][2] = palette[2][2];
        palette[3][3] = 0;
    }

    const uint32_t bits = (uint32_t(src[4]) << 24) | (uint32_t(src[5]) << 16) | (uint32_t(src[6]) << 8) | src[7];
    for (int i = 0; i < 16; i++) {
        const uint32_t index = (bits >> (30 - i * 2)) & 3;
        for (int c = 0; c < 4; c++) {
            colors[i][c] = palette[index][c];
        }
    }
}

std::vector<uint8_t> DecodeRGBA8(TexFormat format, const uint8_t* src, size_t size, uint32_t width, uint32_t height,
                                 const uint8_t* tlut, size_t tlutEntries) {
    CheckSize(format, size, width, height);
    std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4);

    if (format == TexFormat::CMPR) {
        const uint32_t blocksX = (width + 7) / 8;
        const uint32_t blocksY = (height + 7) / 8;
        for (uint32_t by = 0; by < blocksY; by++) {
            for (uint32_t bx = 0; bx < blocksX; bx++) {
                for (uint32_t sub = 0; sub < 4; sub++) {
                    uint8_t colors[16][4];
                    DecodeCmprBlock(src + ((static_cast<size_t>(by) * blocksX + bx) * 4 + sub) * 8, colors);
                    for (uint32_t i = 0; i < 16; i++) {
                        const uint32_t x = bx * 8 + (sub & 1) * 4 + i % 4;
                        const uint32_t y = by * 8 + (sub >> 1) * 4 + i / 4;
                        if (x < width && y < height) {
                            std::copy(colors[i], colors[i] + 4, out.data() + (static_cast<size_t>(y) * width + x) * 4);
                        }
                    }
                }
            }
        }
        return out;
    }

    const auto linear = Detile(format, src, size, width, height);
    if (format == TexFormat::RGBA8) {
        return linear;
    }

    const auto bits = GetBlockInfo(format).bitsPerTexel;
    for (size_t i = 0; i < static_cast<size_t>(width) * height; i++) {
        const uint32_t v = GetTexel(linear.data(), i, bits);
        uint8_t* d = out.data() + i * 4;
        switch (format) {
            case TexFormat::I4:
            case TexFormat::I8: {
                const uint8_t l = format == TexFormat::I4 ? Expand4(v) : (uint8_t)v;
                d[0] = d[1] = d[2] = d[3] = l;
                break;
            }
            case TexFormat::IA4:
                d[0] = d[1] = d[2] = Expand4(v & 0xF);
                d[3] = Expand4(v >> 4);
                break;
            case TexFormat::IA8:
                d[0] = d[1] = d[2] = v & 0xFF;
                d[3] = v >> 8;
                break;
            case TexFormat::RGB565:
                d[0] = Expand5(v >> 11);
                d[1] = Expand6((v >> 5) & 0x3F);
                d[2] = Expand5(v & 0x1F);
                d[3] = 0xFF;
                break;
            case TexFormat::RGB5A3:
                Rgb5a3ToRgba8((uint16_t)v, d);
                break;
            case TexFormat::C4:
            case TexFormat::C8:
            case TexFormat::C14X2: {
                const uint32_t index = format == TexFormat::C14X2 ? (v & 0x3FFF) : v;
                if (tlut == nullptr || index >= tlutEntries) {
                    throw std::runtime_error("Palette index outside the TLUT");
                }
                Rgb5a3ToRgba8(Be16(tlut + index * 2), d);
                break;
            }
            default:
                throw std::runtime_error("Unsupported GX texture format");
        }
    }
    return out;
}

std::optional<N64Format> ParseN64Format(const std::string& name) {
    static const std::unordered_map<std::string, N64Format> formats = {
        { "CI4", N64Format::CI4 },       { "CI8", N64Format::CI8 },       { "I4", N64Format::I4 },
        { "I8", N64Format::I8 },         { "IA8", N64Format::IA8 },       { "IA16", N64Format::IA16 },
        { "RGBA16", N64Format::RGBA16 }, { "RGBA32", N64Format::RGBA32 },
    };
    auto it = formats.find(name);
    if (it == formats.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<TexFormat> SourceFormatFor(N64Format format) {
    switch (format) {
        case N64Format::CI4:
            return TexFormat::C4;
        case N64Format::CI8:
            return TexFormat::C8;
        case N64Format::I4:
            return TexFormat::I4;
        case N64Format::I8:
            return TexFormat::I8;
        case N64Format::IA8:
            return TexFormat::IA4;
        case N64Format::IA16:
            return TexFormat::IA8;
        case N64Format::RGBA16:
            return TexFormat::RGB5A3;
        case N64Format::RGBA32:
            return TexFormat::RGBA8;
    }
    return std::nullopt;
}

bool Rgb5a3TextureIsLossless(const uint8_t* src, size_t size, uint32_t width, uint32_t height) {
    const auto linear = Detile(TexFormat::RGB5A3, src, size, width, height);
    for (size_t i = 0; i < linear.size(); i += 2) {
        if (!Rgb5a3IsLossless(Be16(linear.data() + i))) {
            return false;
        }
    }
    return true;
}

N64Texture ConvertToN64(N64Format format, const uint8_t* src, size_t size, uint32_t width, uint32_t height) {
    const auto source = SourceFormatFor(format).value();
    auto linear = Detile(source, src, size, width, height);

    switch (format) {
        case N64Format::IA8:
            for (auto& b : linear) {
                b = (uint8_t)((b << 4) | (b >> 4));
            }
            break;
        case N64Format::IA16:
            for (size_t i = 0; i + 1 < linear.size(); i += 2) {
                std::swap(linear[i], linear[i + 1]);
            }
            break;
        case N64Format::RGBA16: {
            bool lossless = true;
            for (size_t i = 0; i < linear.size() && lossless; i += 2) {
                lossless = Rgb5a3IsLossless(Be16(linear.data() + i));
            }
            if (!lossless) {
                return { N64Format::RGBA32, DecodeRGBA8(source, src, size, width, height) };
            }
            for (size_t i = 0; i < linear.size(); i += 2) {
                const uint16_t v = Rgb5a3ToRgba5551(Be16(linear.data() + i));
                linear[i] = v >> 8;
                linear[i + 1] = v & 0xFF;
            }
            break;
        }
        default:
            break;
    }
    return { format, std::move(linear) };
}

N64Tlut ConvertTlut(const uint8_t* src, uint32_t count) {
    N64Tlut out = { std::vector<uint8_t>(static_cast<size_t>(count) * 2), 0 };
    for (uint32_t i = 0; i < count; i++) {
        const uint16_t v = Be16(src + i * 2);
        if (Rgb5a3HasPartialAlpha(v)) {
            out.partialAlpha++;
        }
        const uint16_t c = Rgb5a3ToRgba5551(v);
        out.data[i * 2] = c >> 8;
        out.data[i * 2 + 1] = c & 0xFF;
    }
    return out;
}

std::vector<uint8_t> ConvertTlutRgba8(const uint8_t* src, uint32_t count) {
    std::vector<uint8_t> out(static_cast<size_t>(count) * 4);
    for (uint32_t i = 0; i < count; i++) {
        Rgb5a3ToRgba8(Be16(src + i * 2), out.data() + i * 4);
    }
    return out;
}

void ConversionStats::RecordTlut(uint32_t addr, uint32_t partialAlpha) {
    std::lock_guard<std::mutex> lock(mMutex);
    mTluts[addr] = partialAlpha;
}

void ConversionStats::RecordPromotion(uint32_t addr, const std::string& name) {
    std::lock_guard<std::mutex> lock(mMutex);
    mPromotions[addr] = name;
}

uint32_t ConversionStats::GetPartialAlphaEntries() const {
    std::lock_guard<std::mutex> lock(mMutex);
    uint32_t total = 0;
    for (const auto& [addr, count] : mTluts) {
        total += count;
    }
    return total;
}

uint32_t ConversionStats::GetPartialAlphaTluts() const {
    std::lock_guard<std::mutex> lock(mMutex);
    uint32_t total = 0;
    for (const auto& [addr, count] : mTluts) {
        total += count != 0;
    }
    return total;
}

std::map<uint32_t, std::string> ConversionStats::GetPromotions() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mPromotions;
}

void ConversionStats::Clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    mTluts.clear();
    mPromotions.clear();
}

ConversionStats& GetStats() {
    static ConversionStats stats;
    return stats;
}

} // namespace GX
