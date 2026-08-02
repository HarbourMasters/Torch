#include "PlayerClothTextureFactory.h"
#include "AcTextureCodec.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace AC {
namespace {

constexpr uint16_t kWidth = 32;
constexpr uint16_t kHeight = 32;
constexpr size_t kImageSize = 0x200;
constexpr size_t kPaletteSize = 0x20;
constexpr uint32_t kImagePackedOffset = 0;
constexpr uint32_t kPalettePackedOffset = kImageSize;
constexpr size_t kPackedSize = kImageSize + kPaletteSize;
constexpr uint32_t kPaletteEntries = 16;
constexpr uint32_t kPlayerClothCount = 255;
constexpr uint64_t kImageSourceBase = 1454014656;
constexpr uint64_t kPaletteSourceBase = 1453900320;
constexpr const char* kFormat = "C4";
constexpr const char* kPaletteFormat = "RGB5A3";

struct PlayerClothSpecification {
    uint64_t imageSourceOffset;
    uint64_t paletteSourceOffset;
    std::string archivePath;
};

std::string playerClothArchivePath(uint32_t index) {
    char path[64];
    const int length = std::snprintf(path, sizeof(path), "ac/texture/forest_1st/player/cloth-%03u.OTEX", index);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(path)) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE archive path formatting failed");
    }
    return path;
}

PlayerClothSpecification requirePlayerClothSpecification(uint32_t index) {
    if (index >= kPlayerClothCount) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE cloth_index must be between 0 and 254");
    }
    return {
        kImageSourceBase + static_cast<uint64_t>(index) * kImageSize,
        kPaletteSourceBase + static_cast<uint64_t>(index) * kPaletteSize,
        playerClothArchivePath(index),
    };
}

bool isPlayerClothArchivePath(const std::string& path) {
    constexpr std::string_view prefix = "ac/texture/forest_1st/player/cloth-";
    constexpr std::string_view suffix = ".OTEX";
    if (path.size() != prefix.size() + 3U + suffix.size() || path.compare(0, prefix.size(), prefix) != 0 ||
        path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    const size_t digit = prefix.size();
    if (path[digit] < '0' || path[digit] > '9' || path[digit + 1] < '0' || path[digit + 1] > '9' ||
        path[digit + 2] < '0' || path[digit + 2] > '9') {
        return false;
    }
    const uint32_t index = static_cast<uint32_t>(path[digit] - '0') * 100U +
                           static_cast<uint32_t>(path[digit + 1] - '0') * 10U +
                           static_cast<uint32_t>(path[digit + 2] - '0');
    return index < kPlayerClothCount && path == playerClothArchivePath(index);
}

PlayerClothSpecification requireExactConfiguration(YAML::Node& node) {
    if (node["source_base_offset"]) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE does not accept source_base_offset");
    }
    if (GetSafeNode<uint32_t>(node, "offset") != 0) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE generic offset must be packed offset 0");
    }
    const auto specification = requirePlayerClothSpecification(GetSafeNode<uint32_t>(node, "cloth_index"));
    if (GetSafeNode<uint32_t>(node, "width") != kWidth || GetSafeNode<uint32_t>(node, "height") != kHeight) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE requires 32x32 dimensions");
    }
    if (GetSafeNode<std::string>(node, "format") != kFormat ||
        GetSafeNode<std::string>(node, "palette_format") != kPaletteFormat) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE requires C4 with an RGB5A3 palette");
    }
    if (GetSafeNode<uint32_t>(node, "palette_entries") != kPaletteEntries) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE requires exactly 16 palette entries");
    }
    if (GetSafeNode<uint32_t>(node, "image_size") != kImageSize ||
        GetSafeNode<uint32_t>(node, "palette_size") != kPaletteSize) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE selected image and palette ranges must be exact");
    }
    if (GetSafeNode<uint32_t>(node, "image_offset") != kImagePackedOffset ||
        GetSafeNode<uint32_t>(node, "palette_offset") != kPalettePackedOffset) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE packed range offsets must be exact");
    }
    auto ranges = node["bounded_ranges"];
    if (!ranges || !ranges.IsSequence() || ranges.size() != 2) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE requires exactly two bounded source ranges");
    }
    auto image = ranges[0];
    auto palette = ranges[1];
    if (!image.IsMap() || image.size() != 3 ||
        GetSafeNode<uint64_t>(image, "source_offset") != specification.imageSourceOffset ||
        GetSafeNode<uint64_t>(image, "size") != kImageSize ||
        GetSafeNode<uint64_t>(image, "packed_offset") != kImagePackedOffset) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE first bounded range must be the exact image");
    }
    if (!palette.IsMap() || palette.size() != 3 ||
        GetSafeNode<uint64_t>(palette, "source_offset") != specification.paletteSourceOffset ||
        GetSafeNode<uint64_t>(palette, "size") != kPaletteSize ||
        GetSafeNode<uint64_t>(palette, "packed_offset") != kPalettePackedOffset) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE second bounded range must be the exact palette");
    }

    std::string path = GetSafeNode<std::string>(node, "destination_path");
    std::replace(path.begin(), path.end(), '\\', '/');
    constexpr const char* prefix = "__OTR__";
    if (path.rfind(prefix, 0) == 0) {
        path.erase(0, 7);
    }
    if (path != specification.archivePath) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE destination_path must match the exact cloth index");
    }
    return specification;
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> PlayerClothTextureFactory::parse(std::vector<uint8_t>& buffer,
                                                                             YAML::Node& node) {
    const auto specification = requireExactConfiguration(node);

    if (buffer.size() != kPackedSize) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE packed input must be exactly 544 bytes");
    }

    const uint8_t* image = buffer.data() + kImagePackedOffset;
    const uint8_t* palette = buffer.data() + kPalettePackedOffset;
    auto parsed = std::make_shared<PlayerClothTextureData>();
    parsed->archivePath = specification.archivePath;
    parsed->rgba = DecodeC4Rgb5A3(image, kImageSize, palette, kPaletteSize, kWidth, kHeight);
    if (parsed->rgba.size() != 4096U) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE decoded output size is not exact");
    }
    return parsed;
}

ExportResult PlayerClothTextureBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                      std::string& entryName, YAML::Node& /*node*/,
                                                      std::string* replacement) {
    const auto data = std::static_pointer_cast<PlayerClothTextureData>(raw);
    if (!isPlayerClothArchivePath(data->archivePath) || data->rgba.size() != 4096U) {
        throw std::runtime_error("AC:PLAYER_CLOTH_TEXTURE export shape is not exact");
    }
    entryName = data->archivePath;
    if (replacement != nullptr) {
        *replacement = entryName;
    }

    WriteRgba32TextureResource(write, data->rgba, kWidth, kHeight);
    return std::nullopt;
}

} // namespace AC
