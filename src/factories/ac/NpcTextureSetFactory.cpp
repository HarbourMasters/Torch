#include "NpcTextureSetFactory.h"
#include "AcTextureCodec.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace AC {
namespace {

constexpr uint64_t kCompressedSourceOffset = 1447155436ULL;
constexpr uint32_t kCompressedLogicalSize = 6137393U;
constexpr uint32_t kCompressedStoredSize = 6137408U;
constexpr uint32_t kPaletteSize = 0x20U;
constexpr uint32_t kEyeCount = 8U;
constexpr uint32_t kMouthCount = 6U;
constexpr uint32_t kFrameSize = 0x100U;
constexpr uint32_t kBodySize = 0x400U;
constexpr uint32_t kSetSize = kPaletteSize + (kEyeCount + kMouthCount) * kFrameSize + kBodySize;
constexpr uint32_t kMinimumDecompressedSize = 0x4847C0U + kSetSize;
// foresta.rel is a GameCube runtime module and must fit in the console's
// 24 MiB MEM1. Reject impossible Yaz0 declarations before allocating.
constexpr uint32_t kMaximumDecompressedSize = 24U * 1024U * 1024U;
constexpr uint16_t kFrameWidth = 32;
constexpr uint16_t kFrameHeight = 16;
constexpr uint16_t kTextureFormatC4 = 8;
constexpr uint16_t kPaletteFormatRgb5A3 = 2;
constexpr uint32_t kNpcTextureSetType = 0x414E5458U;
constexpr std::array<uint8_t, 4> kPayloadMagic{ 'A', 'C', 'N', 'T' };

constexpr std::array<uint32_t, 15> kCatSetOffsets{ {
    0x474A00U,
    0x475C20U,
    0x47C8E0U,
    0x47DB00U,
    0x47ED20U,
    0x47FF40U,
    0x481160U,
    0x482380U,
    0x4835A0U,
    0x4847C0U,
    0x476E40U,
    0x478060U,
    0x479280U,
    0x47A4A0U,
    0x47B6C0U,
} };

std::string NormalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    constexpr std::string_view prefix = "__OTR__";
    if (path.rfind(prefix, 0) == 0) {
        path.erase(0, prefix.size());
    }
    return path;
}

std::string CatArchivePath(uint32_t variant) {
    char path[64];
    const int length = std::snprintf(path, sizeof(path), "ac/texture/npc/cat/cat-%02u.ANTX", variant);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(path)) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET archive path formatting failed");
    }
    return path;
}

struct Specification {
    uint32_t variant;
    uint32_t setOffset;
    std::string archivePath;
};

Specification RequireExactConfiguration(YAML::Node& node) {
    if (node["source_base_offset"]) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET does not accept source_base_offset");
    }
    if (GetSafeNode<uint32_t>(node, "offset") != 0) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET generic offset must be packed offset 0");
    }
    if (GetSafeNode<std::string>(node, "species") != "cat") {
        throw std::runtime_error("AC:NPC_TEXTURE_SET currently supports the cat family");
    }
    if (GetSafeNode<std::string>(node, "source_member") != "/foresta.rel.szs") {
        throw std::runtime_error("AC:NPC_TEXTURE_SET source member must be foresta.rel.szs");
    }
    if (GetSafeNode<uint32_t>(node, "compressed_logical_size") != kCompressedLogicalSize ||
        GetSafeNode<uint32_t>(node, "compressed_stored_size") != kCompressedStoredSize) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET compressed source size is not exact");
    }
    const uint32_t variant = GetSafeNode<uint32_t>(node, "variant");
    if (variant == 0 || variant > kCatSetOffsets.size()) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET cat variant must be between 1 and 15");
    }
    const uint32_t setOffset = kCatSetOffsets[variant - 1];
    if (GetSafeNode<uint32_t>(node, "texture_set_offset") != setOffset ||
        GetSafeNode<uint32_t>(node, "texture_set_size") != kSetSize) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET selected REL range is not exact");
    }
    if (GetSafeNode<uint32_t>(node, "palette_size") != kPaletteSize ||
        GetSafeNode<uint32_t>(node, "eye_count") != kEyeCount ||
        GetSafeNode<uint32_t>(node, "mouth_count") != kMouthCount ||
        GetSafeNode<uint32_t>(node, "frame_size") != kFrameSize ||
        GetSafeNode<uint32_t>(node, "body_size") != kBodySize ||
        GetSafeNode<uint32_t>(node, "frame_width") != kFrameWidth ||
        GetSafeNode<uint32_t>(node, "frame_height") != kFrameHeight ||
        GetSafeNode<std::string>(node, "format") != "C4" ||
        GetSafeNode<std::string>(node, "palette_format") != "RGB5A3") {
        throw std::runtime_error("AC:NPC_TEXTURE_SET texture layout is not the cat family layout");
    }
    auto ranges = node["bounded_ranges"];
    if (!ranges || !ranges.IsSequence() || ranges.size() != 1) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET requires one bounded source range");
    }
    auto range = ranges[0];
    if (!range.IsMap() || range.size() != 3 ||
        GetSafeNode<uint64_t>(range, "source_offset") != kCompressedSourceOffset ||
        GetSafeNode<uint32_t>(range, "size") != kCompressedStoredSize ||
        GetSafeNode<uint32_t>(range, "packed_offset") != 0) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET bounded source range is not exact");
    }
    const std::string archivePath = NormalizePath(GetSafeNode<std::string>(node, "destination_path"));
    if (archivePath != CatArchivePath(variant)) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET destination path does not match the cat variant");
    }
    return { variant, setOffset, archivePath };
}

void Put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8U));
    out.push_back(static_cast<uint8_t>(value));
}

void Put32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24U));
    out.push_back(static_cast<uint8_t>(value >> 16U));
    out.push_back(static_cast<uint8_t>(value >> 8U));
    out.push_back(static_cast<uint8_t>(value));
}

void Put64(std::vector<uint8_t>& out, uint64_t value) {
    Put32(out, static_cast<uint32_t>(value >> 32U));
    Put32(out, static_cast<uint32_t>(value));
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> NpcTextureSetFactory::parse(std::vector<uint8_t>& buffer,
                                                                        YAML::Node& node) {
    const auto specification = RequireExactConfiguration(node);
    const auto decompressed =
        DecodeYaz0Member(buffer, kCompressedLogicalSize, kCompressedStoredSize, kMinimumDecompressedSize,
                         kMaximumDecompressedSize, "AC:NPC_TEXTURE_SET");
    if (specification.setOffset > decompressed.size() || kSetSize > decompressed.size() - specification.setOffset) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET selected set exceeds decompressed REL");
    }
    auto data = std::make_shared<NpcTextureSetData>();
    data->archivePath = specification.archivePath;
    data->bytes.assign(decompressed.begin() + specification.setOffset,
                       decompressed.begin() + specification.setOffset + kSetSize);
    return data;
}

ExportResult NpcTextureSetBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                 std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto specification = RequireExactConfiguration(node);
    const auto data = std::static_pointer_cast<NpcTextureSetData>(raw);
    if (data->archivePath != specification.archivePath || data->bytes.size() != kSetSize) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET export shape is not exact");
    }
    entryName = data->archivePath;
    if (replacement != nullptr) {
        *replacement = entryName;
    }

    std::vector<uint8_t> out;
    out.reserve(96U + data->bytes.size());
    out.push_back(1);
    out.insert(out.end(), 3, 0);
    Put32(out, kNpcTextureSetType);
    Put32(out, 0);
    Put64(out, 0xDEADBEEFDEADBEEFULL);
    out.resize(64, 0);
    out.insert(out.end(), kPayloadMagic.begin(), kPayloadMagic.end());
    Put16(out, 16);
    Put16(out, kEyeCount);
    Put16(out, kMouthCount);
    Put16(out, kFrameWidth);
    Put16(out, kFrameHeight);
    Put16(out, kTextureFormatC4);
    Put16(out, kPaletteFormatRgb5A3);
    Put16(out, 0);
    Put32(out, kFrameSize);
    Put32(out, kBodySize);
    Put32(out, kSetSize);
    out.insert(out.end(), data->bytes.begin(), data->bytes.end());
    write.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!write) {
        throw std::runtime_error("AC:NPC_TEXTURE_SET binary export failed");
    }
    return std::nullopt;
}

} // namespace AC
