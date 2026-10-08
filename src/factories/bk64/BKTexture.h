#pragma once

#include "BKImage.h"
#include "utils/TextureUtils.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BK64 {

// A texel format sprites or models use, by the name Torch's texture yaml gives it.
struct TexelFormat {
    const char* name;
    int bits;
    TextureType texture;
};

const TexelFormat* FindTexelFormat(const std::string& name);
// A BKTextureInfo type, by its lowest bit set.
const TexelFormat* ModelTextureFormat(uint16_t type);

bool IsIndexed(const TexelFormat& format);
size_t TlutColors(const TexelFormat& format);
size_t TexelBytes(const TexelFormat& format, int width, int height);
uint32_t ReadTexel(const TexelFormat& format, const std::vector<uint8_t>& texels, size_t i);
void WriteTexel(const TexelFormat& format, std::vector<uint8_t>& texels, size_t i, uint32_t value);
void TexelRgba(const TexelFormat& format, uint32_t value, const std::vector<uint8_t>& tlut, uint8_t* out);
bool OpaqueTexel(const TexelFormat& format, uint32_t value, const std::vector<uint8_t>& tlut);
// For CI it also fills the TLUT, from the image's own palette when that fits.
std::vector<uint32_t> ImageTexels(const TexelFormat& format, const RgbaImage& image, const IndexedImage* source,
                                  std::vector<uint8_t>& tlut);

RgbaImage TexelsImage(const TexelFormat& format, int width, int height, const std::vector<uint8_t>& texels,
                      const std::vector<uint8_t>& tlut);
// CI keeps its TLUT as the palette, I goes gray and IA gray with alpha. A value under 0 is a pixel left clear.
std::vector<uint8_t> TexelsPng(const TexelFormat& format, const std::vector<int64_t>& values, int width, int height,
                               const std::vector<uint8_t>& tlut);

std::vector<uint8_t> TextureResource(const TexelFormat& format, uint32_t width, uint32_t height,
                                     const std::vector<uint8_t>& data);
// RGBA16, its colors wide and one high.
std::vector<uint8_t> TlutResource(const std::vector<uint8_t>& tlut);
// format is null for a type no sprite or model uses.
bool ReadTextureResource(const std::vector<uint8_t>& resource, const TexelFormat*& format, uint32_t& width,
                         uint32_t& height, std::vector<uint8_t>& data);

} // namespace BK64
