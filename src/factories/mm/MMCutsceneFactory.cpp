#include "MMCutsceneFactory.h"

#include "MMUtils.h"
#include "spdlog/spdlog.h"

#include <sstream>

namespace MM {

namespace {
constexpr uint32_t kCommandText = 10;
constexpr uint32_t kCommandSpline = 90;
constexpr uint32_t kCommandActorCueMin = 100;
constexpr uint32_t kCommandActorCueMax = 149;
constexpr uint32_t kCommandPlayerCueMin = 200;
constexpr uint32_t kCommandPlayerCueMax = 201;
constexpr uint32_t kCommandTransition = 155;
constexpr uint32_t kCommandFadeOutSequence = 156;
constexpr uint32_t kCommandTime = 157;
constexpr uint32_t kCommandRumble = 400;
constexpr uint32_t kCommandAdditionalActorCueMin = 450;
constexpr uint32_t kCommandAdditionalActorCueMax = 599;
constexpr uint32_t kCommandTerminator = 0xFFFFFFFF;
constexpr uint16_t kSplineTerminatorEntryCount = 0xFFFF;
constexpr uint16_t kSplineTerminatorMarker = 4;
} // namespace

class MMCutsceneData : public IParsedData {
public:
    std::vector<char> binary;
    explicit MMCutsceneData(std::vector<char> data) : binary(std::move(data)) {}
};

static bool IsActorCue(uint32_t id) {
    return (id >= kCommandActorCueMin && id <= kCommandActorCueMax) ||
           (id >= kCommandPlayerCueMin && id <= kCommandPlayerCueMax) ||
           (id >= kCommandAdditionalActorCueMin && id <= kCommandAdditionalActorCueMax);
}

static bool IsSmallEntry(uint32_t id) {
    return id == kCommandText || id == kCommandTransition || id == kCommandFadeOutSequence || id == kCommandTime ||
           id == kCommandRumble;
}

static bool Skip(LUS::BinaryReader& reader, uint32_t size) {
    if (reader.GetBaseAddress() + size > reader.GetLength()) {
        return false;
    }
    reader.Seek(reader.GetBaseAddress() + size, LUS::SeekOffsetType::Start);
    return true;
}

static bool SkipSpline(LUS::BinaryReader& reader) {
    while (true) {
        if (reader.GetBaseAddress() + 4 > reader.GetLength()) {
            return false;
        }
        const uint16_t entryCount = reader.ReadUInt16();
        const uint16_t marker = reader.ReadUInt16();
        if (entryCount == kSplineTerminatorEntryCount && marker == kSplineTerminatorMarker) {
            return true;
        }
        if (!Skip(reader, 4) || !Skip(reader, entryCount * 32)) {
            return false;
        }
    }
}

static std::optional<uint32_t> GetSize(std::vector<uint8_t>& buffer, uint32_t offset) {
    auto reader = ReadSubArray(buffer, offset, 0x10000);
    if (reader.GetLength() < 8) {
        return std::nullopt;
    }
    const uint32_t commandCount = reader.ReadUInt32();
    reader.ReadUInt32();
    for (uint32_t i = 0; i < commandCount; ++i) {
        if (reader.GetBaseAddress() + 4 > reader.GetLength()) {
            return std::nullopt;
        }
        const uint32_t id = reader.ReadUInt32();
        if (id == kCommandTerminator) {
            return reader.GetBaseAddress();
        }
        if (reader.GetBaseAddress() + 4 > reader.GetLength()) {
            return std::nullopt;
        }
        const uint32_t entryCount = reader.ReadUInt32();
        if (id == kCommandSpline) {
            if (!SkipSpline(reader)) {
                return std::nullopt;
            }
        } else if (IsActorCue(id)) {
            if (!Skip(reader, entryCount * 48)) {
                return std::nullopt;
            }
        } else if (IsSmallEntry(id)) {
            if (!Skip(reader, entryCount * 12)) {
                return std::nullopt;
            }
        } else if (!Skip(reader, entryCount * 8)) {
            return std::nullopt;
        }
    }
    if (reader.GetBaseAddress() + 4 > reader.GetLength() || reader.ReadUInt32() != kCommandTerminator) {
        return std::nullopt;
    }
    return reader.GetBaseAddress();
}

std::optional<std::shared_ptr<IParsedData>> MMCutsceneFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const uint32_t offset = GetSafeNode<uint32_t>(node, "offset");
    const auto size = node["size"] ? std::optional<uint32_t>(node["size"].as<uint32_t>()) : GetSize(buffer, offset);
    if (!size.has_value()) {
        SPDLOG_ERROR("MMCutsceneFactory: could not determine cutscene size");
        return std::nullopt;
    }

    auto reader = ReadSubArray(buffer, offset, size.value());
    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::OoTCutscene, 0);
    writer.Write(static_cast<uint32_t>(0));
    const uint32_t wordCountStart = writer.GetStream()->GetLength();
    const auto writeHH = [&](uint16_t first, uint16_t second) {
        writer.Write(first);
        writer.Write(second);
    };
    const auto writeHBB = [&](uint16_t first, uint8_t second, uint8_t third) {
        writer.Write(first);
        writer.Write(second);
        writer.Write(third);
    };
    const auto writeBBBB = [&](uint8_t first, uint8_t second, uint8_t third, uint8_t fourth) {
        writer.Write(first);
        writer.Write(second);
        writer.Write(third);
        writer.Write(fourth);
    };

    const uint32_t commandCount = reader.ReadUInt32();
    writer.Write(commandCount);
    writer.Write(reader.ReadUInt32());

    for (uint32_t command = 0; command < commandCount; ++command) {
        const uint32_t id = reader.ReadUInt32();
        writer.Write(id);

        if (id == kCommandSpline) {
            writer.Write(reader.ReadUInt32());
            while (true) {
                const uint16_t entryCount = reader.ReadUInt16();
                const uint16_t unused0 = reader.ReadUInt16();
                if (entryCount == kSplineTerminatorEntryCount) {
                    writeHH(kSplineTerminatorEntryCount, kSplineTerminatorMarker);
                    break;
                }
                const uint16_t unused1 = reader.ReadUInt16();
                const uint16_t duration = reader.ReadUInt16();
                writeHH(entryCount, unused0);
                writeHH(unused1, duration);

                for (uint32_t entry = 0; entry < entryCount * 2; ++entry) {
                    const uint8_t interpType = reader.ReadUByte();
                    const uint8_t weight = reader.ReadUByte();
                    const uint16_t frameDuration = reader.ReadUInt16();
                    const uint16_t posX = reader.ReadUInt16();
                    const uint16_t posY = reader.ReadUInt16();
                    const uint16_t posZ = reader.ReadUInt16();
                    const uint16_t relativeTo = reader.ReadUInt16();
                    writer.Write(interpType);
                    writer.Write(weight);
                    writer.Write(frameDuration);
                    writeHH(posX, posY);
                    writeHH(posZ, relativeTo);
                }
                for (uint32_t entry = 0; entry < entryCount; ++entry) {
                    const uint16_t value0 = reader.ReadUInt16();
                    const uint16_t roll = reader.ReadUInt16();
                    const uint16_t fov = reader.ReadUInt16();
                    const uint16_t value1 = reader.ReadUInt16();
                    writeHH(value0, roll);
                    writeHH(fov, value1);
                }
            }
            continue;
        }

        const uint32_t entryCount = reader.ReadUInt32();
        writer.Write(entryCount);
        for (uint32_t entry = 0; entry < entryCount; ++entry) {
            const uint16_t base = reader.ReadUInt16();
            const uint16_t startFrame = reader.ReadUInt16();
            const uint16_t endFrame = reader.ReadUInt16();

            if (IsActorCue(id)) {
                const uint16_t rotX = reader.ReadUInt16();
                const uint16_t rotY = reader.ReadUInt16();
                const uint16_t rotZ = reader.ReadUInt16();
                writeHH(base, startFrame);
                writeHH(endFrame, rotX);
                writeHH(rotY, rotZ);
                for (size_t value = 0; value < 9; ++value) {
                    writer.Write(reader.ReadUInt32());
                }
                continue;
            }

            switch (id) {
                case kCommandText: {
                    const uint16_t type = reader.ReadUInt16();
                    const uint16_t textId1 = reader.ReadUInt16();
                    const uint16_t textId2 = reader.ReadUInt16();
                    writeHH(base, startFrame);
                    writeHH(endFrame, type);
                    writeHH(textId1, textId2);
                    break;
                }
                case kCommandTransition: {
                    const uint8_t value0 = reader.ReadUByte();
                    const uint8_t value1 = reader.ReadUByte();
                    const uint8_t value2 = reader.ReadUByte();
                    reader.ReadUByte();
                    reader.ReadUInt16();
                    writeHH(base, startFrame);
                    writeHBB(endFrame, value0, value1);
                    writeBBBB(value2, 0, 0, 0);
                    break;
                }
                case kCommandFadeOutSequence: {
                    const uint16_t pad = reader.ReadUInt16();
                    reader.ReadUInt32();
                    writeHH(base, startFrame);
                    writeHH(endFrame, pad);
                    writer.Write(static_cast<uint32_t>(0));
                    break;
                }
                case kCommandTime: {
                    const uint8_t hour = reader.ReadUByte();
                    const uint8_t minute = reader.ReadUByte();
                    reader.ReadUInt32();
                    writeHH(base, startFrame);
                    writeHBB(endFrame, hour, minute);
                    writer.Write(static_cast<uint32_t>(0));
                    break;
                }
                case kCommandRumble: {
                    const uint8_t intensity = reader.ReadUByte();
                    const uint8_t timer = reader.ReadUByte();
                    const uint8_t step = reader.ReadUByte();
                    reader.ReadUByte();
                    reader.ReadUInt16();
                    writeHH(base, startFrame);
                    writeHBB(endFrame, intensity, timer);
                    writeBBBB(step, 0, 0, 0);
                    break;
                }
                default: {
                    const uint16_t pad = reader.ReadUInt16();
                    writeHH(base, startFrame);
                    writeHH(endFrame, pad);
                    break;
                }
            }
        }
    }
    writer.Write(reader.ReadUInt32());
    writer.Write(static_cast<uint32_t>(0));
    const uint32_t end = writer.GetStream()->GetLength();
    writer.Seek(wordCountStart - 4, LUS::SeekOffsetType::Start);
    writer.Write(static_cast<uint32_t>((end - wordCountStart) / 4));
    writer.Seek(end, LUS::SeekOffsetType::Start);

    std::stringstream stream;
    writer.Finish(stream);
    const std::string serialized = stream.str();
    return std::make_shared<MMCutsceneData>(std::vector<char>(serialized.begin(), serialized.end()));
}

ExportResult MMCutsceneBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string&, YAML::Node&, std::string*) {
    const auto cutscene = std::static_pointer_cast<MMCutsceneData>(raw);
    write.write(cutscene->binary.data(), cutscene->binary.size());
    return std::nullopt;
}

} // namespace MM
