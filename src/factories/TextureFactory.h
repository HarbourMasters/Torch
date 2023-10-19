#pragma once

#include "RawFactory.h"

enum class TextureType {
    Error,
    RGBA32bpp,
    RGBA16bpp,
    Palette4bpp,
    Palette8bpp,
    Grayscale4bpp,
    Grayscale8bpp,
    GrayscaleAlpha1bpp,
    GrayscaleAlpha4bpp,
    GrayscaleAlpha8bpp,
    GrayscaleAlpha16bpp,
};

class TextureFactory : public RawFactory {
public:
    TextureFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};