#include "ItemBillboardTextureFactory.h"
#include "AcTextureCodec.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace AC {
namespace {

constexpr uint64_t kCompressedSourceOffset = 1447155436ULL;
constexpr uint32_t kCompressedLogicalSize = 6137393U;
constexpr uint32_t kCompressedStoredSize = 6137408U;
constexpr uint32_t kMaximumDecompressedSize = 24U * 1024U * 1024U;
constexpr uint32_t kPaletteSize = 0x20U;
constexpr uint32_t kPaletteEntries = 16U;

struct Member {
    std::string_view name;
    uint32_t textureOffset;
    uint32_t paletteOffset;
    uint16_t width;
};

constexpr std::array<Member, 49> kMembers{ {
    { "apple", 0x66AB40U, 0x66AB20U, 32 },    { "axe", 0xB6AAA0U, 0xB6AA80U, 32 },
    { "axe2", 0xB6AD60U, 0xB6AD40U, 32 },     { "bag", 0xB6DC20U, 0xB6DC00U, 32 },
    { "bone", 0xB6DEE0U, 0xB6DEC0U, 32 },     { "box", 0xB6E1A0U, 0xB6E180U, 32 },
    { "cage", 0xB6E460U, 0xB6E440U, 32 },     { "carpet", 0xB6B020U, 0xB6B000U, 32 },
    { "cloth", 0xB6EA00U, 0xB6E9E0U, 32 },    { "coco", 0xB6ECC0U, 0xB6ECA0U, 32 },
    { "diary", 0xB6EF80U, 0xB6EF60U, 32 },    { "fish", 0x66BD18U, 0x66BCF8U, 32 },
    { "fork", 0xB6F560U, 0xB6F540U, 32 },     { "fossil", 0xB6F820U, 0xB6F800U, 32 },
    { "fuku", 0xB6B2E0U, 0xB6B2C0U, 32 },     { "haniwa", 0xB6FAE0U, 0xB6FAC0U, 32 },
    { "kabu", 0x66C9C0U, 0x66C9A0U, 32 },     { "kaza", 0xB6B5A0U, 0xB6B580U, 32 },
    { "leaf", 0xB70060U, 0xB70040U, 32 },     { "matutake", 0x672A80U, 0x672A60U, 32 },
    { "net", 0xB6B860U, 0xB6B840U, 32 },      { "net2", 0xB6BB20U, 0xB6BB00U, 32 },
    { "nuts", 0xB708A0U, 0xB70880U, 32 },     { "omikuji", 0xB70B60U, 0xB70B40U, 32 },
    { "orange", 0x673C00U, 0x673BE0U, 32 },   { "other", 0xB710E0U, 0xB710C0U, 32 },
    { "otosi", 0xB713A0U, 0xB71380U, 32 },    { "pack", 0xB71660U, 0xB71640U, 32 },
    { "paper", 0xB6BDE0U, 0xB6BDC0U, 32 },    { "peach", 0xB71920U, 0xB71900U, 32 },
    { "pear", 0xB71BE0U, 0xB71BC0U, 32 },     { "present", 0xB71EA0U, 0xB71E80U, 32 },
    { "rod", 0xB6C0A0U, 0xB6C080U, 32 },      { "rod2", 0xB6C360U, 0xB6C340U, 32 },
    { "roll", 0xB72160U, 0xB72140U, 32 },     { "seed", 0xB6C620U, 0xB6C600U, 32 },
    { "shell-a", 0xB72560U, 0xB72540U, 16 },  { "shell-b", 0xB726A0U, 0xB72680U, 16 },
    { "shell-c", 0xB727E0U, 0xB727C0U, 16 },  { "shovel", 0xB6C8E0U, 0xB6C8C0U, 32 },
    { "shovel2", 0xB6CBA0U, 0xB6CB80U, 32 },  { "taisou", 0xB6CE60U, 0xB6CE40U, 32 },
    { "tane", 0x3ECA60U, 0x3ECA40U, 16 },     { "ticket", 0xB6D120U, 0xB6D100U, 32 },
    { "tool", 0xB72920U, 0xB72900U, 32 },     { "trash", 0xB72BE0U, 0xB72BC0U, 32 },
    { "umbrella", 0xB72EA0U, 0xB72E80U, 32 }, { "utiwa", 0xB6D3E0U, 0xB6D3C0U, 32 },
    { "wall", 0xB6D6A0U, 0xB6D680U, 32 },
} };

struct Specification {
    uint32_t textureOffset;
    uint32_t paletteOffset;
    uint32_t textureSize;
    uint16_t width;
    uint16_t height;
    std::string archivePath;
};

std::string NormalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    constexpr std::string_view prefix = "__OTR__";
    if (path.rfind(prefix, 0) == 0) {
        path.erase(0, prefix.size());
    }
    return path;
}

Specification RequireExactConfiguration(YAML::Node& node) {
    if (node["source_base_offset"]) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE does not accept source_base_offset");
    }
    if (GetSafeNode<uint32_t>(node, "offset") != 0U) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE generic offset must be packed offset 0");
    }
    if (GetSafeNode<std::string>(node, "source_member") != "/foresta.rel.szs") {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE source member must be foresta.rel.szs");
    }
    if (GetSafeNode<uint32_t>(node, "compressed_logical_size") != kCompressedLogicalSize ||
        GetSafeNode<uint32_t>(node, "compressed_stored_size") != kCompressedStoredSize) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE compressed source size is not exact");
    }

    const std::string itemName = GetSafeNode<std::string>(node, "item_name");
    const auto member = std::find_if(kMembers.begin(), kMembers.end(),
                                     [&](const Member& candidate) { return candidate.name == itemName; });
    if (member == kMembers.end()) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE item_name is not in the supported family");
    }
    const uint32_t textureSize = static_cast<uint32_t>(member->width) * member->width / 2U;
    if (GetSafeNode<uint32_t>(node, "width") != member->width ||
        GetSafeNode<uint32_t>(node, "height") != member->width ||
        GetSafeNode<uint32_t>(node, "texture_offset") != member->textureOffset ||
        GetSafeNode<uint32_t>(node, "palette_offset") != member->paletteOffset ||
        GetSafeNode<uint32_t>(node, "texture_size") != textureSize ||
        GetSafeNode<uint32_t>(node, "palette_size") != kPaletteSize) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE member layout differs from the supported family");
    }
    if (GetSafeNode<std::string>(node, "format") != "C4" ||
        GetSafeNode<std::string>(node, "palette_format") != "RGB5A3" ||
        GetSafeNode<uint32_t>(node, "palette_entries") != kPaletteEntries) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE requires C4 with a 16-entry RGB5A3 palette");
    }

    auto ranges = node["bounded_ranges"];
    if (!ranges || !ranges.IsSequence() || ranges.size() != 1U) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE requires one bounded source range");
    }
    auto range = ranges[0];
    if (!range.IsMap() || range.size() != 3U ||
        GetSafeNode<uint64_t>(range, "source_offset") != kCompressedSourceOffset ||
        GetSafeNode<uint32_t>(range, "size") != kCompressedStoredSize ||
        GetSafeNode<uint32_t>(range, "packed_offset") != 0U) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE bounded source range is not exact");
    }

    const std::string archivePath = NormalizePath(GetSafeNode<std::string>(node, "destination_path"));
    const std::string expectedPath = "ac/texture/item/" + itemName + ".OTEX";
    if (archivePath != expectedPath) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE destination path does not match item_name");
    }
    return {
        member->textureOffset, member->paletteOffset, textureSize, member->width, member->width, archivePath,
    };
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> ItemBillboardTextureFactory::parse(std::vector<uint8_t>& buffer,
                                                                               YAML::Node& node) {
    const auto specification = RequireExactConfiguration(node);
    const uint32_t minimumOutputSize =
        std::max(specification.textureOffset + specification.textureSize, specification.paletteOffset + kPaletteSize);
    const auto decompressed = DecodeYaz0Member(buffer, kCompressedLogicalSize, kCompressedStoredSize, minimumOutputSize,
                                               kMaximumDecompressedSize, "AC:ITEM_BILLBOARD_TEXTURE");

    auto data = std::make_shared<ItemBillboardTextureData>();
    data->archivePath = specification.archivePath;
    data->width = specification.width;
    data->height = specification.height;
    data->rgba = DecodeC4Rgb5A3(decompressed.data() + specification.textureOffset, specification.textureSize,
                                decompressed.data() + specification.paletteOffset, kPaletteSize, specification.width,
                                specification.height);
    return data;
}

ExportResult ItemBillboardTextureBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                        std::string& entryName, YAML::Node& node,
                                                        std::string* replacement) {
    const auto specification = RequireExactConfiguration(node);
    const auto data = std::static_pointer_cast<ItemBillboardTextureData>(raw);
    const size_t expectedSize = static_cast<size_t>(specification.width) * specification.height * 4U;
    if (data->archivePath != specification.archivePath || data->width != specification.width ||
        data->height != specification.height || data->rgba.size() != expectedSize) {
        throw std::runtime_error("AC:ITEM_BILLBOARD_TEXTURE export shape is not exact");
    }
    entryName = data->archivePath;
    if (replacement != nullptr) {
        *replacement = entryName;
    }
    WriteRgba32TextureResource(write, data->rgba, data->width, data->height);
    return std::nullopt;
}

} // namespace AC
