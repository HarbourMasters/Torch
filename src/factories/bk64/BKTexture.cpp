#include "BKTexture.h"

#include "BKByteUtils.h"
#include "BKResource.h"
#include "factories/BaseFactory.h"

#include <algorithm>

namespace BK64 {
namespace {
const TexelFormat sTexelFormats[] = {
    { "CI4", 4, TextureType::Palette4bpp },        { "CI8", 8, TextureType::Palette8bpp },
    { "I4", 4, TextureType::Grayscale4bpp },       { "I8", 8, TextureType::Grayscale8bpp },
    { "IA4", 4, TextureType::GrayscaleAlpha4bpp }, { "IA8", 8, TextureType::GrayscaleAlpha8bpp },
    { "RGBA16", 16, TextureType::RGBA16bpp },      { "RGBA32", 32, TextureType::RGBA32bpp },
};

void from5551(uint16_t color, uint8_t* out) {
    out[0] = ExpandBits(color >> 11 & 0x1F, 5);
    out[1] = ExpandBits(color >> 6 & 0x1F, 5);
    out[2] = ExpandBits(color >> 1 & 0x1F, 5);
    out[3] = (color & 1) ? 255 : 0;
}

uint16_t to5551(const uint8_t* p) {
    return (uint16_t)(CompressBits(p[0], 5) << 11 | CompressBits(p[1], 5) << 6 | CompressBits(p[2], 5) << 1 |
                      (p[3] >= 128 ? 1 : 0));
}

uint16_t tlutColor(const std::vector<uint8_t>& tlut, uint32_t index) {
    return index * 2 + 1 < tlut.size() ? ReadU16BE(&tlut[index * 2]) : 0;
}

bool gray(const TexelFormat& format) {
    return format.texture == TextureType::Grayscale4bpp || format.texture == TextureType::Grayscale8bpp;
}

bool grayAlpha(const TexelFormat& format) {
    return format.texture == TextureType::GrayscaleAlpha4bpp || format.texture == TextureType::GrayscaleAlpha8bpp;
}

// Not for CI4 and CI8, which go through a palette
uint32_t rgbaTexel(const TexelFormat& format, const uint8_t* p) {
    const uint32_t luma = (p[0] * 299 + p[1] * 587 + p[2] * 114 + 500) / 1000;
    // An intensity texel is its own alpha in game, so a clear pixel goes dark
    const uint8_t covered = (uint8_t)((luma * p[3] + 127) / 255);
    switch (format.texture) {
        case TextureType::Grayscale4bpp:
            return CompressBits(covered, 4);
        case TextureType::Grayscale8bpp:
            return covered;
        case TextureType::GrayscaleAlpha4bpp:
            return CompressBits((uint8_t)luma, 3) << 1 | (p[3] >= 128 ? 1 : 0);
        case TextureType::GrayscaleAlpha8bpp:
            return CompressBits((uint8_t)luma, 4) << 4 | CompressBits(p[3], 4);
        case TextureType::RGBA16bpp:
            return to5551(p);
        default:
            return ReadU32BE(p);
    }
}
} // namespace

const TexelFormat* FindTexelFormat(const std::string& name) {
    for (const auto& format : sTexelFormats) {
        if (name == format.name) {
            return &format;
        }
    }
    return nullptr;
}

// textureInfo_getBitDepth tests the type field bit by bit and takes the first one set, so a
// texture with extra bits still resolves to a format. The game's ladder stops at 0x8; Torch
// extends to 0x10.
const TexelFormat* ModelTextureFormat(uint16_t type) {
    static const std::pair<uint16_t, const char*> kTypes[] = {
        { 0x1, "CI4" }, { 0x2, "CI8" }, { 0x4, "RGBA16" }, { 0x8, "RGBA32" }, { 0x10, "IA8" },
    };
    for (const auto& [bit, name] : kTypes) {
        if (type & bit) {
            return FindTexelFormat(name);
        }
    }
    return nullptr;
}

bool IsIndexed(const TexelFormat& format) {
    return format.texture == TextureType::Palette4bpp || format.texture == TextureType::Palette8bpp;
}

size_t TlutColors(const TexelFormat& format) {
    return format.bits == 4 ? 16 : 256;
}

size_t TexelBytes(const TexelFormat& format, int width, int height) {
    return (size_t)std::max(width, 0) * std::max(height, 0) * format.bits / 8;
}

uint32_t ReadTexel(const TexelFormat& format, const std::vector<uint8_t>& texels, size_t i) {
    switch (format.bits) {
        case 4:
            return i / 2 < texels.size() ? (texels[i / 2] >> (i % 2 ? 0 : 4)) & 0xF : 0;
        case 8:
            return i < texels.size() ? texels[i] : 0;
        case 16:
            return i * 2 + 1 < texels.size() ? ReadU16BE(&texels[i * 2]) : 0;
        default:
            return i * 4 + 3 < texels.size() ? ReadU32BE(&texels[i * 4]) : 0;
    }
}

void WriteTexel(const TexelFormat& format, std::vector<uint8_t>& texels, size_t i, uint32_t value) {
    switch (format.bits) {
        case 4:
            if (i / 2 < texels.size()) {
                const int shift = i % 2 ? 0 : 4;
                texels[i / 2] = (uint8_t)((texels[i / 2] & ~(0xF << shift)) | (value & 0xF) << shift);
            }
            break;
        case 8:
            if (i < texels.size()) {
                texels[i] = (uint8_t)value;
            }
            break;
        case 16:
            if (i * 2 + 1 < texels.size()) {
                texels[i * 2] = (uint8_t)(value >> 8);
                texels[i * 2 + 1] = (uint8_t)value;
            }
            break;
        default:
            if (i * 4 + 3 < texels.size()) {
                for (int b = 0; b < 4; b++) {
                    texels[i * 4 + b] = (uint8_t)(value >> (24 - b * 8));
                }
            }
            break;
    }
}

void TexelRgba(const TexelFormat& format, uint32_t value, const std::vector<uint8_t>& tlut, uint8_t* out) {
    switch (format.texture) {
        case TextureType::Palette4bpp:
        case TextureType::Palette8bpp:
            from5551(tlutColor(tlut, value), out);
            return;
        case TextureType::Grayscale4bpp:
        case TextureType::Grayscale8bpp:
            out[0] = out[1] = out[2] = format.bits == 4 ? ExpandBits(value, 4) : (uint8_t)value;
            out[3] = 255;
            return;
        case TextureType::GrayscaleAlpha4bpp:
            out[0] = out[1] = out[2] = ExpandBits(value >> 1, 3);
            out[3] = (value & 1) ? 255 : 0;
            return;
        case TextureType::GrayscaleAlpha8bpp:
            out[0] = out[1] = out[2] = ExpandBits(value >> 4, 4);
            out[3] = ExpandBits(value & 0xF, 4);
            return;
        case TextureType::RGBA16bpp:
            from5551((uint16_t)value, out);
            return;
        default:
            for (int b = 0; b < 4; b++) {
                out[b] = (uint8_t)(value >> (24 - b * 8));
            }
            return;
    }
}

bool OpaqueTexel(const TexelFormat& format, uint32_t value, const std::vector<uint8_t>& tlut) {
    switch (format.texture) {
        case TextureType::Palette4bpp:
        case TextureType::Palette8bpp:
            return tlutColor(tlut, value) & 1;
        case TextureType::GrayscaleAlpha4bpp:
        case TextureType::RGBA16bpp:
            return value & 1;
        case TextureType::GrayscaleAlpha8bpp:
            return (value & 0xF) != 0;
        case TextureType::RGBA32bpp:
            return (value & 0xFF) != 0;
        default:
            return value != 0;
    }
}

std::vector<uint32_t> ImageTexels(const TexelFormat& format, const RgbaImage& image, const IndexedImage* source,
                                  std::vector<uint8_t>& tlut) {
    const size_t pixels = (size_t)image.width * image.height;
    std::vector<uint32_t> out(pixels, 0);
    if (!IsIndexed(format)) {
        for (size_t i = 0; i < pixels; i++) {
            out[i] = rgbaTexel(format, &image.pixels[i * 4]);
        }
        return out;
    }
    const size_t colors = TlutColors(format);
    if (source != nullptr && (source->width != image.width || source->height != image.height)) {
        source = nullptr;
    }
    if (source != nullptr) {
        // Entries past the TLUT can only be the clear one an export adds for pixels no chunk covers
        const size_t entries = source->palette.size() / 4;
        for (size_t e = colors; e < entries; e++) {
            if (source->palette[e * 4 + 3] != 0) {
                source = nullptr;
                break;
            }
        }
    }
    IndexedImage quantized;
    if (source == nullptr) {
        quantized = Quantize(image, (int)colors);
        source = &quantized;
    }
    tlut.assign(colors * 2, 0);
    const size_t entries = std::min(colors, source->palette.size() / 4);
    uint32_t clear = 0;
    bool foundClear = false;
    for (size_t e = 0; e < entries; e++) {
        const uint16_t color = to5551(&source->palette[e * 4]);
        tlut[e * 2] = (uint8_t)(color >> 8);
        tlut[e * 2 + 1] = (uint8_t)color;
        if (!foundClear && !(color & 1)) {
            clear = (uint32_t)e;
            foundClear = true;
        }
    }
    for (size_t i = 0; i < pixels; i++) {
        out[i] = source->indices[i] < colors ? source->indices[i] : clear;
    }
    return out;
}

RgbaImage TexelsImage(const TexelFormat& format, int width, int height, const std::vector<uint8_t>& texels,
                      const std::vector<uint8_t>& tlut) {
    RgbaImage image{ width, height, std::vector<uint8_t>((size_t)std::max(width, 0) * std::max(height, 0) * 4, 0) };
    for (size_t i = 0; i < (size_t)std::max(width, 0) * std::max(height, 0); i++) {
        TexelRgba(format, ReadTexel(format, texels, i), tlut, &image.pixels[i * 4]);
    }
    return image;
}

std::vector<uint8_t> TexelsPng(const TexelFormat& format, const std::vector<int64_t>& values, int width, int height,
                               const std::vector<uint8_t>& tlut) {
    const size_t pixels = (size_t)width * height;
    if (IsIndexed(format)) {
        IndexedImage image{ width, height, std::vector<uint8_t>(pixels, 0), {} };
        int clear = -1;
        for (size_t e = 0; e < TlutColors(format); e++) {
            uint8_t rgba[4];
            from5551(tlutColor(tlut, (uint32_t)e), rgba);
            image.palette.insert(image.palette.end(), rgba, rgba + 4);
            if (clear < 0 && rgba[3] == 0) {
                clear = (int)e;
            }
        }
        if (clear < 0 && std::any_of(values.begin(), values.end(), [](int64_t v) { return v < 0; })) {
            // No clear entry for the pixels no chunk covers, so the image gets one past the TLUT
            clear = (int)TlutColors(format);
            image.palette.insert(image.palette.end(), { 0, 0, 0, 0 });
        }
        for (size_t i = 0; i < pixels; i++) {
            image.indices[i] = (uint8_t)(values[i] < 0 ? clear : values[i]);
        }
        return EncodeIndexedPng(image);
    }
    if (gray(format)) {
        std::vector<uint8_t> channel(pixels, 0);
        for (size_t i = 0; i < pixels; i++) {
            if (values[i] >= 0) {
                channel[i] = format.bits == 4 ? ExpandBits((uint32_t)values[i], 4) : (uint8_t)values[i];
            }
        }
        return EncodeGrayPng(width, height, channel, false);
    }
    const bool intensity = grayAlpha(format);
    std::vector<uint8_t> channels(pixels * (intensity ? 2 : 4), 0);
    for (size_t i = 0; i < pixels; i++) {
        if (values[i] < 0) {
            continue;
        }
        uint8_t rgba[4];
        TexelRgba(format, (uint32_t)values[i], tlut, rgba);
        if (intensity) {
            channels[i * 2] = rgba[0];
            channels[i * 2 + 1] = rgba[3];
        } else {
            std::copy_n(rgba, 4, &channels[i * 4]);
        }
    }
    return intensity ? EncodeGrayPng(width, height, channels, true)
                     : EncodePng(RgbaImage{ width, height, std::move(channels) });
}

std::vector<uint8_t> TextureResource(const TexelFormat& format, uint32_t width, uint32_t height,
                                     const std::vector<uint8_t>& data) {
    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::Texture, 0);
    writer.Write((uint32_t)format.texture);
    writer.Write(width);
    writer.Write(height);
    writer.Write((uint32_t)data.size());
    writer.Write((char*)data.data(), data.size());
    const auto bytes = writer.ToVector();
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

std::vector<uint8_t> TlutResource(const std::vector<uint8_t>& tlut) {
    return TextureResource(*FindTexelFormat("RGBA16"), (uint32_t)tlut.size() / 2, 1, tlut);
}

bool ReadTextureResource(const std::vector<uint8_t>& resource, const TexelFormat*& format, uint32_t& width,
                         uint32_t& height, std::vector<uint8_t>& data) {
    try {
        ResourceReader reader(resource);
        const auto type = (TextureType)reader.Read<uint32_t>();
        width = reader.Read<uint32_t>();
        height = reader.Read<uint32_t>();
        data = reader.Bytes(reader.Read<uint32_t>());
        format = nullptr;
        for (const auto& known : sTexelFormats) {
            if (known.texture == type) {
                format = &known;
            }
        }
        return true;
    } catch (const std::exception&) { return false; }
}

} // namespace BK64
