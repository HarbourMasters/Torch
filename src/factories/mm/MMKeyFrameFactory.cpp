#include "MMKeyFrameFactory.h"

#include "MMUtils.h"
#include "Companion.h"
#include "spdlog/spdlog.h"

#include <sstream>

namespace MM {

class MMKeyFrameData : public IParsedData {
public:
    std::vector<char> binary;
    explicit MMKeyFrameData(std::vector<char> data) : binary(std::move(data)) {}
};

static std::optional<uint8_t> GetLimbType(YAML::Node& node) {
    const auto type = GetSafeNode<std::string>(node, "limb_type");
    if (type == "Normal" || type == "normal") {
        return 0;
    }
    if (type == "Flex" || type == "flex") {
        return 1;
    }
    SPDLOG_ERROR("MMKeyFrameFactory: limb_type must be Normal or Flex");
    return std::nullopt;
}

static uint32_t CountBits(uint16_t value) {
    uint32_t count = 0;
    while (value != 0) {
        count += value & 1;
        value >>= 1;
    }
    return count;
}

std::optional<std::shared_ptr<IParsedData>> MMKeyFrameSkeletonFactory::parse(std::vector<uint8_t>& buffer,
                                                                               YAML::Node& node) {
    const auto type = GetLimbType(node);
    if (!type.has_value()) {
        return std::nullopt;
    }
    auto reader = ReadSubArray(buffer, GetSafeNode<uint32_t>(node, "offset"), 8);
    const uint8_t limbCount = reader.ReadUByte();
    const uint8_t dListCount = reader.ReadUByte();
    reader.ReadUInt16();
    const uint32_t limbsAddress = reader.ReadUInt32();
    if (limbsAddress == 0 || !IS_SEGMENTED(limbsAddress)) {
        return std::nullopt;
    }
    auto limbs = ReadSubArray(buffer, limbsAddress, limbCount * (type.value() == 0 ? 12 : 8));

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::MMKeyFrameSkeleton, 0);
    writer.Write(limbCount);
    writer.Write(dListCount);
    writer.Write(type.value());
    writer.Write(limbCount);
    for (uint8_t i = 0; i < limbCount; ++i) {
        const uint32_t dListAddress = Companion::Instance->PatchVirtualAddr(limbs.ReadUInt32());
        std::string path;
        if (dListAddress != 0) {
            const auto resolved = Companion::Instance->GetStringByAddr(dListAddress);
            if (!resolved.has_value()) {
                SPDLOG_ERROR("MMKeyFrameSkeletonFactory: could not resolve display list 0x{:08X}", dListAddress);
                return std::nullopt;
            }
            path = resolved.value();
        }
        writer.Write(path);
        writer.Write(limbs.ReadUByte());
        writer.Write(limbs.ReadUByte());
        if (type.value() == 0) {
            writer.Write(limbs.ReadInt16());
            writer.Write(limbs.ReadInt16());
            writer.Write(limbs.ReadInt16());
        } else {
            writer.Write(limbs.ReadUByte());
            limbs.ReadUByte();
        }
    }

    std::stringstream stream;
    writer.Finish(stream);
    const std::string serialized = stream.str();
    return std::make_shared<MMKeyFrameData>(std::vector<char>(serialized.begin(), serialized.end()));
}

std::optional<std::shared_ptr<IParsedData>> MMKeyFrameAnimationFactory::parse(std::vector<uint8_t>& buffer,
                                                                                YAML::Node& node) {
    const auto type = GetLimbType(node);
    if (!type.has_value()) {
        return std::nullopt;
    }
    uint32_t limbCount = GetSafeNode<uint32_t>(node, "limb_count", 0);
    if (limbCount == 0) {
        const std::string skeletonOffsetKey = node["skel_offset"] ? "skel_offset" : "skeleton_offset";
        uint32_t skeletonOffset = GetSafeNode<uint32_t>(node, skeletonOffsetKey);
        const uint32_t animationOffset = GetSafeNode<uint32_t>(node, "offset");
        if (!IS_SEGMENTED(skeletonOffset) && IS_SEGMENTED(animationOffset)) {
            skeletonOffset |= animationOffset & 0xFF000000;
        }
        auto skeleton = ReadSubArray(buffer, skeletonOffset, 1);
        limbCount = skeleton.ReadUByte();
    }
    auto reader = ReadSubArray(buffer, GetSafeNode<uint32_t>(node, "offset"), 0x14);
    const uint32_t flagsAddress = reader.ReadUInt32();
    const uint32_t keyFramesAddress = reader.ReadUInt32();
    const uint32_t keyFrameNumbersAddress = reader.ReadUInt32();
    const uint32_t presetValuesAddress = reader.ReadUInt32();
    const uint16_t unknown = reader.ReadUInt16();
    const uint16_t duration = reader.ReadUInt16();
    const uint32_t flagSize = type.value() == 0 ? limbCount : limbCount * 2;
    auto flags = ReadSubArray(buffer, flagsAddress, flagSize);
    std::vector<uint16_t> values;
    values.reserve(limbCount);
    uint32_t keyFrameNumberCount = 0;
    uint32_t presetValueCount = 0;
    for (uint32_t i = 0; i < limbCount; ++i) {
        const uint16_t value = type.value() == 0 ? flags.ReadUByte() : flags.ReadUInt16();
        values.push_back(value);
        const uint16_t mask = type.value() == 0 ? 0x003F : 0x01FF;
        keyFrameNumberCount += CountBits(value & mask);
        presetValueCount += CountBits((value ^ (type.value() == 0 ? 0x00FF : 0xFFFF)) & mask);
    }
    auto keyFrameNumbers = ReadSubArray(buffer, keyFrameNumbersAddress, keyFrameNumberCount * 2);
    std::vector<int16_t> counts;
    counts.reserve(keyFrameNumberCount);
    uint32_t keyFrameCount = 0;
    for (uint32_t i = 0; i < keyFrameNumberCount; ++i) {
        const int16_t count = keyFrameNumbers.ReadInt16();
        if (count < 0) {
            return std::nullopt;
        }
        counts.push_back(count);
        keyFrameCount += count;
    }
    auto keyFrames = ReadSubArray(buffer, keyFramesAddress, keyFrameCount * 6);
    auto presetValues = ReadSubArray(buffer, presetValuesAddress, presetValueCount * 2);

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::MMKeyFrameAnimation, 0);
    writer.Write(type.value());
    writer.Write(static_cast<uint32_t>(values.size()));
    for (const uint16_t value : values) {
        if (type.value() == 0) {
            writer.Write(static_cast<uint8_t>(value));
        } else {
            writer.Write(value);
        }
    }
    writer.Write(keyFrameCount);
    for (uint32_t i = 0; i < keyFrameCount; ++i) {
        writer.Write(keyFrames.ReadInt16());
        writer.Write(keyFrames.ReadInt16());
        writer.Write(keyFrames.ReadInt16());
    }
    writer.Write(static_cast<uint32_t>(counts.size()));
    for (const int16_t count : counts) {
        writer.Write(count);
    }
    writer.Write(presetValueCount);
    for (uint32_t i = 0; i < presetValueCount; ++i) {
        writer.Write(presetValues.ReadInt16());
    }
    writer.Write(unknown);
    writer.Write(duration);

    std::stringstream stream;
    writer.Finish(stream);
    const std::string serialized = stream.str();
    return std::make_shared<MMKeyFrameData>(std::vector<char>(serialized.begin(), serialized.end()));
}

ExportResult MMKeyFrameSkeletonBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                       std::string&, YAML::Node&, std::string*) {
    const auto data = std::static_pointer_cast<MMKeyFrameData>(raw);
    write.write(data->binary.data(), data->binary.size());
    return std::nullopt;
}

ExportResult MMKeyFrameAnimationBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                        std::string&, YAML::Node&, std::string*) {
    const auto data = std::static_pointer_cast<MMKeyFrameData>(raw);
    write.write(data->binary.data(), data->binary.size());
    return std::nullopt;
}

} // namespace MM
