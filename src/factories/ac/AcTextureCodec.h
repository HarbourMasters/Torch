#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string_view>
#include <vector>

namespace AC {

std::vector<uint8_t> DecodeC4Rgb5A3(const uint8_t* image, size_t imageSize, const uint8_t* palette, size_t paletteSize,
                                    uint16_t width, uint16_t height);

std::vector<uint8_t> DecodeC8Rgb5A3(const uint8_t* image, size_t imageSize, const uint8_t* palette, size_t paletteSize,
                                    uint16_t paletteEntries, uint16_t width, uint16_t height);

std::vector<uint8_t> DecodeYaz0Member(const std::vector<uint8_t>& source, uint32_t logicalSize, uint32_t storedSize,
                                      uint32_t minimumOutputSize, uint32_t maximumOutputSize, std::string_view owner);

void WriteRgba32TextureResource(std::ostream& write, const std::vector<uint8_t>& rgba, uint16_t width, uint16_t height);

} // namespace AC
