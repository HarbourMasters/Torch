#include "BtiTextureFactory.h"
#include "AcTextureCodec.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace AC {
namespace {

constexpr uint16_t kWidth = 32;
constexpr uint16_t kHeight = 64;
constexpr uint16_t kPaletteEntries = 176;
constexpr size_t kHeaderSize = 32;
constexpr size_t kImageOffset = 0x20;
constexpr size_t kImageSize = 2048;
constexpr size_t kPaletteOffset = 0x820;
constexpr size_t kPaletteSize = 352;
constexpr uint64_t kSourceOffset = 1454147680;
constexpr uint64_t kSourceSize = 2432;
constexpr const char* kArchivePath = "ac/texture/forest_2nd/data/boy1.OTEX";
constexpr std::array<uint8_t, kHeaderSize> kHeader{
    0x09, 0x02, 0x00, 0x20, 0x00, 0x40, 0x00, 0x00, 0x01, 0x02, 0x00, 0xB0, 0x00, 0x00, 0x08, 0x20,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
};

void RequireExactConfiguration(YAML::Node& node, size_t bufferSize) {
    if (node["source_base_offset"]) {
        throw std::runtime_error("AC:BTI_TEXTURE does not accept source_base_offset");
    }
    if (GetSafeNode<uint32_t>(node, "offset") != 0 || GetSafeNode<uint32_t>(node, "size") != kSourceSize ||
        bufferSize != kSourceSize) {
        throw std::runtime_error("AC:BTI_TEXTURE requires the exact packed boy1 range");
    }
    auto ranges = node["bounded_ranges"];
    if (!ranges || !ranges.IsSequence() || ranges.size() != 1) {
        throw std::runtime_error("AC:BTI_TEXTURE requires exactly one bounded source range");
    }
    auto range = ranges[0];
    if (!range.IsMap() || range.size() != 3 || GetSafeNode<uint64_t>(range, "source_offset") != kSourceOffset ||
        GetSafeNode<uint64_t>(range, "size") != kSourceSize || GetSafeNode<uint64_t>(range, "packed_offset") != 0) {
        throw std::runtime_error("AC:BTI_TEXTURE bounded source range must be the exact boy1 member");
    }

    std::string path = GetSafeNode<std::string>(node, "destination_path");
    std::replace(path.begin(), path.end(), '\\', '/');
    constexpr const char* prefix = "__OTR__";
    if (path.rfind(prefix, 0) == 0) {
        path.erase(0, 7);
    }
    if (path != kArchivePath) {
        throw std::runtime_error("AC:BTI_TEXTURE destination_path must be the exact boy1 OTEX path");
    }
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> BtiTextureFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    RequireExactConfiguration(node, buffer.size());
    if (!std::equal(kHeader.begin(), kHeader.end(), buffer.begin())) {
        throw std::runtime_error("AC:BTI_TEXTURE requires the exact boy1 BTI header");
    }

    auto parsed = std::make_shared<BtiTextureData>();
    parsed->width = kWidth;
    parsed->height = kHeight;
    parsed->archivePath = kArchivePath;
    parsed->rgba = DecodeC8Rgb5A3(buffer.data() + kImageOffset, kImageSize, buffer.data() + kPaletteOffset,
                                  kPaletteSize, kPaletteEntries, kWidth, kHeight);
    if (parsed->rgba.size() != 8192U) {
        throw std::runtime_error("AC:BTI_TEXTURE decoded output size is not exact");
    }
    return parsed;
}

ExportResult BtiTextureBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& /*node*/, std::string* replacement) {
    const auto data = std::static_pointer_cast<BtiTextureData>(raw);
    if (data->width != kWidth || data->height != kHeight || data->archivePath != kArchivePath ||
        data->rgba.size() != 8192U) {
        throw std::runtime_error("AC:BTI_TEXTURE export shape is not exact");
    }
    entryName = data->archivePath;
    if (replacement != nullptr) {
        *replacement = entryName;
    }

    WriteRgba32TextureResource(write, data->rgba, data->width, data->height);
    return std::nullopt;
}

} // namespace AC
