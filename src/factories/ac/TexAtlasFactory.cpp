#include "TexAtlasFactory.h"
#include "ACUtils.h"
#include "utils/TorchUtils.h"

#ifdef GX_TEXTURE_SUPPORT
#include "gc/GXTexture.h"
#endif

#include <algorithm>
#include <map>

namespace AC {

std::optional<std::shared_ptr<IParsedData>> TexAtlasFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
#ifdef GX_TEXTURE_SUPPORT
    static const std::map<std::string, TextureFormat> formats = {
        { "CI4", { TextureType::Palette4bpp, 4 } },        { "CI8", { TextureType::Palette8bpp, 8 } },
        { "I4", { TextureType::Grayscale4bpp, 4 } },       { "I8", { TextureType::Grayscale8bpp, 8 } },
        { "IA4", { TextureType::GrayscaleAlpha4bpp, 4 } }, { "IA8", { TextureType::GrayscaleAlpha8bpp, 8 } },
        { "IA16", { TextureType::GrayscaleAlpha16bpp, 16 } }, { "RGBA16", { TextureType::RGBA16bpp, 16 } },
    };
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto size = GetSafeNode<uint32_t>(node, "size");
    const auto name = NameOf(node);
    auto atlas = ReadBytes(offset, size, name);

    if (!node["regions"] || !node["regions"].IsSequence() || node["regions"].size() == 0) {
        throw std::runtime_error(name + ": AC:TEX_ATLAS needs at least one region");
    }
    std::optional<TextureFormat> format;
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    for (auto region : node["regions"]) {
        const auto off = GetSafeNode<uint32_t>(region, "offset");
        auto fmt = Upper(GetSafeNode<std::string>(region, "format"));
        const auto width = GetSafeNode<uint32_t>(region, "width");
        const auto height = GetSafeNode<uint32_t>(region, "height");
        const auto it = formats.find(fmt);
        const auto n64 = GX::ParseN64Format(fmt);
        if (it == formats.end() || !n64.has_value()) {
            throw std::runtime_error(name + ": region format " + fmt + " is not supported in an atlas");
        }
        if (format.has_value() && format->depth != it->second.depth) {
            throw std::runtime_error(name + ": atlas regions mix texel depths");
        }
        format = it->second;

        const auto source = GX::SourceFormatFor(n64.value()).value();
        const uint32_t tiled = (uint32_t)GX::TiledSize(source, width, height);
        const uint32_t linear = width * height * it->second.depth / 8;
        if (tiled != linear) {
            throw std::runtime_error(name + ": region at +0x" + Torch::to_hex(off, false) + " (" + fmt + " " +
                                     std::to_string(width) + "x" + std::to_string(height) +
                                     ") is not a whole number of GX tiles");
        }
        if (off + tiled > size) {
            throw std::runtime_error(name + ": region at +0x" + Torch::to_hex(off, false) + " overruns the atlas");
        }
        for (const auto& [start, end] : spans) {
            if (off < end && start < off + tiled) {
                throw std::runtime_error(name + ": regions overlap at +0x" + Torch::to_hex(off, false));
            }
        }
        spans.emplace_back(off, off + tiled);

        auto converted = GX::ConvertToN64(n64.value(), atlas.data() + off, tiled, width, height);
        if (converted.format != n64.value() || converted.data.size() != tiled) {
            throw std::runtime_error(name + ": region at +0x" + Torch::to_hex(off, false) +
                                     " changes size when converted (RGBA32 promotion)");
        }
        std::copy(converted.data.begin(), converted.data.end(), atlas.begin() + off);
    }
    return std::make_shared<TextureData>(format.value(), size * 8 / format->depth, 1, atlas);
#else
    throw std::runtime_error("AC:TEX_ATLAS needs a build with BUILD_GAMECUBE or BUILD_AC");
#endif
}

} // namespace AC
