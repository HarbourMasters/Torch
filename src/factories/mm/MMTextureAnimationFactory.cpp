#include "MMTextureAnimationFactory.h"

#include "MMUtils.h"
#include "Companion.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <sstream>

namespace MM {

namespace {
constexpr int16_t kMaterialTypeTexScroll = 0;
constexpr int16_t kMaterialTypeTwoTexScroll = 1;
constexpr int16_t kMaterialTypeColor = 2;
constexpr int16_t kMaterialTypeColorLerp = 3;
constexpr int16_t kMaterialTypeColorNonLinearInterp = 4;
constexpr int16_t kMaterialTypeTexCycle = 5;
constexpr int16_t kMaterialTypeEmpty = 6;
constexpr uint32_t kTexScrollLayerCount = 1;
constexpr uint32_t kTwoTexScrollLayerCount = 2;
constexpr uint32_t kDefaultMaxEntries = 256;
} // namespace

class MMTextureAnimationData : public IParsedData {
public:
    std::vector<char> binary;
    explicit MMTextureAnimationData(std::vector<char> data) : binary(std::move(data)) {}
};

static bool WriteScroll(LUS::BinaryWriter& writer, std::vector<uint8_t>& buffer, uint32_t address, uint32_t count) {
    if (address == 0) {
        return false;
    }

    auto reader = ReadSubArray(buffer, address, count * 4);
    for (uint32_t i = 0; i < count; ++i) {
        writer.Write(reader.ReadInt8());
        writer.Write(reader.ReadInt8());
        writer.Write(reader.ReadUByte());
        writer.Write(reader.ReadUByte());
    }
    return true;
}

static bool WriteColors(LUS::BinaryWriter& writer, std::vector<uint8_t>& buffer, uint32_t address, uint16_t type) {
    if (address == 0) {
        return false;
    }

    auto reader = ReadSubArray(buffer, address, 0x10);
    const uint16_t animationLength = reader.ReadUInt16();
    const uint16_t colorListCount = reader.ReadUInt16();
    const uint32_t primAddress = reader.ReadUInt32();
    const uint32_t envAddress = reader.ReadUInt32();
    const uint32_t frameAddress = reader.ReadUInt32();
    const uint16_t listLength = type == kMaterialTypeColor ? animationLength : colorListCount;

    writer.Write(animationLength);
    writer.Write(colorListCount);

    if (frameAddress == 0) {
        writer.Write(static_cast<uint16_t>(0));
    } else {
        auto frames = ReadSubArray(buffer, frameAddress, listLength * 2);
        writer.Write(listLength);
        for (uint16_t i = 0; i < listLength; ++i) {
            writer.Write(frames.ReadUInt16());
        }
    }

    if (primAddress == 0) {
        writer.Write(static_cast<uint16_t>(0));
    } else {
        auto colors = ReadSubArray(buffer, primAddress, listLength * 5);
        writer.Write(listLength);
        for (uint16_t i = 0; i < listLength; ++i) {
            writer.Write(colors.ReadUByte());
            writer.Write(colors.ReadUByte());
            writer.Write(colors.ReadUByte());
            writer.Write(colors.ReadUByte());
            writer.Write(colors.ReadUByte());
        }
    }

    if (envAddress == 0) {
        writer.Write(static_cast<uint16_t>(0));
    } else {
        auto colors = ReadSubArray(buffer, envAddress, listLength * 4);
        writer.Write(listLength);
        for (uint16_t i = 0; i < listLength; ++i) {
            writer.Write(colors.ReadUByte());
            writer.Write(colors.ReadUByte());
            writer.Write(colors.ReadUByte());
            writer.Write(colors.ReadUByte());
        }
    }
    return true;
}

static bool WriteCycle(LUS::BinaryWriter& writer, std::vector<uint8_t>& buffer, uint32_t address) {
    if (address == 0) {
        return false;
    }

    auto reader = ReadSubArray(buffer, address, 0x0C);
    const uint16_t cycleLength = reader.ReadUInt16();
    reader.ReadUInt16();
    const uint32_t texturesAddress = reader.ReadUInt32();
    const uint32_t indicesAddress = reader.ReadUInt32();
    if (cycleLength == 0 || texturesAddress == 0 || indicesAddress == 0) {
        return false;
    }

    auto indices = ReadSubArray(buffer, indicesAddress, cycleLength);
    std::vector<uint8_t> values;
    values.reserve(cycleLength);
    uint8_t maxIndex = 0;
    for (uint16_t i = 0; i < cycleLength; ++i) {
        const uint8_t index = indices.ReadUByte();
        values.push_back(index);
        maxIndex = std::max(maxIndex, index);
    }

    auto textures = ReadSubArray(buffer, texturesAddress, (maxIndex + 1) * 4);
    writer.Write(cycleLength);
    writer.Write(static_cast<uint32_t>(maxIndex + 1));
    for (uint32_t i = 0; i <= maxIndex; ++i) {
        const uint32_t pointer = Companion::Instance->PatchVirtualAddr(textures.ReadUInt32());
        const auto path = Companion::Instance->GetStringByAddr(pointer);
        if (!path.has_value()) {
            SPDLOG_ERROR("MMTextureAnimationFactory: could not resolve texture 0x{:08X}", pointer);
            return false;
        }
        writer.Write(path.value());
    }
    for (const uint8_t index : values) {
        writer.Write(index);
    }
    return true;
}

std::optional<std::shared_ptr<IParsedData>> MMTextureAnimationFactory::parse(std::vector<uint8_t>& buffer,
                                                                               YAML::Node& node) {
    const uint32_t offset = GetSafeNode<uint32_t>(node, "offset");
    const uint32_t maxEntries = GetSafeNode<uint32_t>(node, "max_entries", kDefaultMaxEntries);
    struct Entry {
        int8_t segment;
        int16_t type;
        uint32_t paramsAddress;
    };
    std::vector<Entry> entries;
    for (uint32_t i = 0; i < maxEntries; ++i) {
        auto reader = ReadSubArray(buffer, offset + i * 8, 8);
        const int8_t segment = reader.ReadInt8();
        reader.ReadUByte();
        const int16_t type = reader.ReadInt16();
        const uint32_t paramsAddress = reader.ReadUInt32();
        entries.push_back({ segment, type, paramsAddress });
        if (segment <= 0) {
            break;
        }
    }
    if (entries.empty() || entries.back().segment > 0) {
        SPDLOG_ERROR("MMTextureAnimationFactory: no terminating animation entry within {} entries", maxEntries);
        return std::nullopt;
    }

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::MMTextureAnimation, 0);
    writer.Write(static_cast<uint32_t>(entries.size()));
    for (const auto& entry : entries) {
        writer.Write(entry.segment);
        writer.Write(entry.type);

        if (entry.segment <= 0 && entry.type == kMaterialTypeTexScroll && entry.paramsAddress == 0) {
            writer.Write(static_cast<uint32_t>(0));
            continue;
        }

        bool succeeded = false;
        switch (entry.type) {
            case kMaterialTypeTexScroll:
                succeeded = WriteScroll(writer, buffer, entry.paramsAddress, kTexScrollLayerCount);
                break;
            case kMaterialTypeTwoTexScroll:
                succeeded = WriteScroll(writer, buffer, entry.paramsAddress, kTwoTexScrollLayerCount);
                break;
            case kMaterialTypeColor:
            case kMaterialTypeColorLerp:
            case kMaterialTypeColorNonLinearInterp:
                succeeded = WriteColors(writer, buffer, entry.paramsAddress, entry.type);
                break;
            case kMaterialTypeTexCycle:
                succeeded = WriteCycle(writer, buffer, entry.paramsAddress);
                break;
            case kMaterialTypeEmpty:
                writer.Write(static_cast<uint32_t>(0));
                succeeded = entry.paramsAddress == 0;
                break;
            default:
                SPDLOG_ERROR("MMTextureAnimationFactory: unsupported animation type {}", entry.type);
                break;
        }
        if (!succeeded) {
            return std::nullopt;
        }
    }

    std::stringstream stream;
    writer.Finish(stream);
    const std::string serialized = stream.str();
    return std::make_shared<MMTextureAnimationData>(std::vector<char>(serialized.begin(), serialized.end()));
}

ExportResult MMTextureAnimationBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                       std::string&, YAML::Node&, std::string*) {
    const auto animation = std::static_pointer_cast<MMTextureAnimationData>(raw);
    write.write(animation->binary.data(), animation->binary.size());
    return std::nullopt;
}

} // namespace MM
