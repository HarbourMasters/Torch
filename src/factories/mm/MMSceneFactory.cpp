#include "MMSceneFactory.h"

#include "MMUtils.h"
#include "Companion.h"
#include "factories/oot/OoTSceneCommandWriter.h"
#include "factories/oot/OoTSceneFactory.h"
#include "utils/StringHelper.h"

#include <iomanip>
#include <set>
#include <sstream>

namespace MM {

namespace {
constexpr uint8_t kCommandStartPositionList = 0;
constexpr uint8_t kCommandActorList = 1;
constexpr uint8_t kCommandRoomList = 4;
constexpr uint8_t kCommandRoomBehavior = 8;
constexpr uint8_t kCommandObjectList = 11;
constexpr uint8_t kCommandLightList = 12;
constexpr uint8_t kCommandPathways = 13;
constexpr uint8_t kCommandTransitionActorList = 14;
constexpr uint8_t kCommandLightingSettings = 15;
constexpr uint8_t kCommandEndMarker = 20;
constexpr uint8_t kCommandCutscenes = 23;
constexpr uint8_t kCommandCameraSettings = 25;
constexpr uint8_t kCommandAnimatedMaterial = 26;
constexpr uint8_t kCommandActorCutscenes = 27;
constexpr uint8_t kCommandMinimapList = 28;
constexpr uint8_t kCommandMinimapChests = 30;
constexpr uint8_t kArchiveCommandCutscenes = 31;
} // namespace

class MMSceneData : public IParsedData {
public:
    std::vector<OoT::SceneCommand> commands;
};

static uint32_t NeighborCount(const std::set<uint32_t>& addresses, uint32_t address, uint32_t entrySize) {
    const auto offset = SEGMENT_OFFSET(address);
    const auto next = addresses.upper_bound(offset);
    return next == addresses.end() ? 0 : (*next - offset) / entrySize;
}

static std::set<uint32_t> CollectAddresses(const std::vector<std::pair<uint32_t, uint32_t>>& commands) {
    std::set<uint32_t> addresses;
    for (const auto& [word, pointer] : commands) {
        const uint8_t id = word >> 24;
        const uint8_t count = word >> 16;
        if (pointer == 0 || !IS_SEGMENTED(pointer)) {
            continue;
        }
        const uint32_t offset = SEGMENT_OFFSET(pointer);
        addresses.insert(offset);
        switch (id) {
            case kCommandStartPositionList:
            case kCommandActorList:
            case kCommandTransitionActorList:
                addresses.insert(offset + count * 16);
                break;
            case kCommandRoomList:
                addresses.insert(offset + count * 8);
                break;
            case kCommandObjectList:
                addresses.insert(offset + count * 2);
                break;
            case kCommandLightList:
                addresses.insert(offset + count * 14);
                break;
            case kCommandLightingSettings:
                addresses.insert(offset + count * 22);
                break;
            case kCommandActorCutscenes:
                addresses.insert(offset + count * 16);
                break;
            case kCommandMinimapChests:
                addresses.insert(offset + count * 10);
                break;
        }
    }
    return addresses;
}

static OoT::SceneCommand Finish(uint8_t id, LUS::BinaryWriter& writer) {
    std::stringstream stream;
    writer.Finish(stream);
    const auto serialized = stream.str();
    return { id, std::vector<uint8_t>(serialized.begin(), serialized.end()) };
}

static std::string Resolve(uint32_t address) {
    const auto path = Companion::Instance->GetStringByAddr(Companion::Instance->PatchVirtualAddr(address));
    return path.value_or("");
}

static std::string AddAsset(uint32_t address, const std::string& type, const std::string& symbol,
                            uint32_t numPaths = 0) {
    if (const auto path = Resolve(address); !path.empty()) {
        return path;
    }
    YAML::Node asset;
    asset["type"] = type;
    asset["offset"] = address;
    asset["symbol"] = symbol;
    if (numPaths != 0) {
        asset["num_paths"] = numPaths;
    }
    const auto added = Companion::Instance->AddAsset(asset);
    if (!added.has_value() || !added.value()["vpath"]) {
        return "";
    }
    return added.value()["vpath"].as<std::string>();
}

static OoT::SceneCommand WriteRoomBehavior(uint32_t word, uint32_t value) {
    LUS::BinaryWriter writer;
    writer.Write(static_cast<int8_t>(word >> 16));
    writer.Write(static_cast<int8_t>(value & 0xFF));
    writer.Write(static_cast<int8_t>((value >> 8) & 1));
    writer.Write(static_cast<int8_t>((value >> 10) & 1));
    writer.Write(static_cast<int8_t>((value >> 11) & 1));
    writer.Write(static_cast<int8_t>((value >> 12) & 1));
    return Finish(kCommandRoomBehavior, writer);
}

static OoT::SceneCommand WriteRoomList(uint8_t count, uint32_t address, std::vector<uint8_t>& buffer,
                                       const std::string& directory) {
    auto reader = ReadSubArray(buffer, address, count * 8);
    const auto slash = directory.find_last_of('/');
    auto base = slash == std::string::npos ? directory : directory.substr(slash + 1);
    if (base.size() > 6 && base.ends_with("_scene")) {
        base.resize(base.size() - 6);
    }
    LUS::BinaryWriter writer;
    writer.Write(static_cast<uint32_t>(count));
    for (uint8_t i = 0; i < count; ++i) {
        std::ostringstream name;
        name << directory << '/' << base << "_room_" << std::setw(2) << std::setfill('0') << static_cast<uint32_t>(i);
        writer.Write(name.str());
        writer.Write(reader.ReadUInt32());
        writer.Write(reader.ReadUInt32());
    }
    return Finish(kCommandRoomList, writer);
}

static OoT::SceneCommand WriteLightList(uint8_t count, uint32_t address, std::vector<uint8_t>& buffer) {
    auto reader = ReadSubArray(buffer, address, count * 14);
    LUS::BinaryWriter writer;
    writer.Write(static_cast<uint32_t>(count));
    for (uint8_t i = 0; i < count; ++i) {
        writer.Write(reader.ReadUByte());
        reader.ReadUByte();
        writer.Write(reader.ReadInt16());
        writer.Write(reader.ReadInt16());
        writer.Write(reader.ReadInt16());
        writer.Write(reader.ReadUByte());
        writer.Write(reader.ReadUByte());
        writer.Write(reader.ReadUByte());
        writer.Write(reader.ReadUByte());
        writer.Write(reader.ReadInt16());
    }
    return Finish(kCommandLightList, writer);
}

static OoT::SceneCommand WritePathways(uint32_t address, uint32_t count, std::vector<uint8_t>& buffer,
                                       const std::string& symbol) {
    if (count == 0) {
        LUS::BinaryWriter writer;
        writer.Write(static_cast<uint32_t>(0));
        return Finish(kCommandPathways, writer);
    }

    auto reader = ReadSubArray(buffer, address, count * 8);
    std::vector<std::string> paths;
    paths.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        reader.ReadUByte();
        reader.ReadUByte();
        reader.ReadInt16();
        const auto pointsAddress = reader.ReadUInt32();
        if (!IS_SEGMENTED(pointsAddress)) {
            break;
        }
        paths.push_back(AddAsset(pointsAddress, "MM:PATH", symbol + "List" + std::to_string(i)));
    }

    LUS::BinaryWriter writer;
    writer.Write(static_cast<uint32_t>(paths.size()));
    for (const auto& path : paths) {
        writer.Write(path);
    }
    return Finish(kCommandPathways, writer);
}

static OoT::SceneCommand WriteCutscenes(uint8_t count, uint32_t address, std::vector<uint8_t>& buffer,
                                        const std::string& symbol) {
    auto reader = ReadSubArray(buffer, address, count * 8);
    LUS::BinaryWriter writer;
    writer.Write(count);
    for (uint8_t i = 0; i < count; ++i) {
        const uint32_t cutscene = reader.ReadUInt32();
        const auto path = AddAsset(cutscene, "MM:CUTSCENE", symbol + "Cutscene" + std::to_string(i));
        writer.Write(path);
        writer.Write(reader.ReadUInt16());
        writer.Write(reader.ReadUByte());
        writer.Write(reader.ReadUByte());
    }
    return Finish(kArchiveCommandCutscenes, writer);
}

static OoT::SceneCommand WriteAnimatedMaterial(uint32_t address, const std::string& symbol) {
    LUS::BinaryWriter writer;
    writer.Write(AddAsset(address, "MM:TEXTURE_ANIMATION", symbol));
    return Finish(kCommandAnimatedMaterial, writer);
}

static OoT::SceneCommand WriteMinimapList(uint32_t address, uint32_t roomCount, std::vector<uint8_t>& buffer) {
    auto list = ReadSubArray(buffer, address, 8);
    const uint32_t entriesAddress = list.ReadUInt32();
    const uint16_t scale = list.ReadUInt16();
    auto entries = ReadSubArray(buffer, entriesAddress, roomCount * 10);
    LUS::BinaryWriter writer;
    writer.Write(roomCount);
    writer.Write(scale);
    for (uint32_t i = 0; i < roomCount; ++i) {
        for (uint32_t j = 0; j < 5; ++j) {
            writer.Write(entries.ReadUInt16());
        }
    }
    return Finish(kCommandMinimapList, writer);
}

static OoT::SceneCommand WriteMinimapChests(uint8_t count, uint32_t address, std::vector<uint8_t>& buffer) {
    auto reader = ReadSubArray(buffer, address, count * 10);
    LUS::BinaryWriter writer;
    writer.Write(static_cast<uint32_t>(count));
    for (uint8_t i = 0; i < count; ++i) {
        for (uint32_t j = 0; j < 5; ++j) {
            writer.Write(reader.ReadUInt16());
        }
    }
    return Finish(kCommandMinimapChests, writer);
}

static OoT::SceneCommand WriteActorCutscenes(uint8_t count, uint32_t address, std::vector<uint8_t>& buffer) {
    auto reader = ReadSubArray(buffer, address, count * 16);
    LUS::BinaryWriter writer;
    writer.Write(static_cast<uint32_t>(count));
    for (uint8_t i = 0; i < count; ++i) {
        for (uint32_t j = 0; j < 5; ++j) {
            writer.Write(reader.ReadInt16());
        }
        writer.Write(reader.ReadUByte());
        writer.Write(reader.ReadUByte());
        writer.Write(reader.ReadInt16());
        writer.Write(reader.ReadUByte());
        writer.Write(reader.ReadUByte());
    }
    return Finish(kCommandActorCutscenes, writer);
}

std::optional<std::shared_ptr<IParsedData>> MMSceneFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto reader = ReadSubArray(buffer, GetSafeNode<uint32_t>(node, "offset"), 0x10000);
    std::vector<std::pair<uint32_t, uint32_t>> raw;
    while (reader.GetBaseAddress() + 8 <= reader.GetLength()) {
        const uint32_t word = reader.ReadUInt32();
        const uint32_t value = reader.ReadUInt32();
        raw.emplace_back(word, value);
        if ((word >> 24) == kCommandEndMarker) {
            break;
        }
    }
    const auto knownAddresses = CollectAddresses(raw);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto directory = Companion::Instance->GetCurrentDirectory();
    const auto type = GetSafeNode<std::string>(node, "type");
    std::vector<OoT::PendingAltHeader> pending;
    OoT::SceneWriteContext context { buffer, knownAddresses, symbol, symbol, directory, type, false, pending };
    OoT::SceneCommandWriter sharedWriter;
    auto scene = std::make_shared<MMSceneData>();
    uint32_t roomCount = 0;
    for (const auto& [word, value] : raw) {
        const uint8_t id = word >> 24;
        const uint8_t count = word >> 16;
        if (id == kCommandRoomList) {
            roomCount = count;
        }
        switch (id) {
            case kCommandRoomList:
                scene->commands.push_back(WriteRoomList(count, value, buffer, directory));
                break;
            case kCommandRoomBehavior:
                scene->commands.push_back(WriteRoomBehavior(word, value));
                break;
            case kCommandLightList:
                scene->commands.push_back(WriteLightList(count, value, buffer));
                break;
            case kCommandPathways: {
                const uint32_t pathCount = GetSafeNode<uint32_t>(node, "path_count", NeighborCount(knownAddresses, value, 8));
                scene->commands.push_back(WritePathways(value, pathCount, buffer, symbol + "Pathway"));
                break;
            }
            case kCommandCutscenes:
                scene->commands.push_back(WriteCutscenes(count, value, buffer, symbol));
                break;
            case kCommandCameraSettings:
                scene->commands.push_back({ id, {} });
                break;
            case kCommandAnimatedMaterial:
                scene->commands.push_back(WriteAnimatedMaterial(value, symbol + "TextureAnimation"));
                break;
            case kCommandActorCutscenes:
                scene->commands.push_back(WriteActorCutscenes(count, value, buffer));
                break;
            case kCommandMinimapList:
                scene->commands.push_back(WriteMinimapList(value, roomCount, buffer));
                break;
            case kCommandMinimapChests:
                scene->commands.push_back(WriteMinimapChests(count, value, buffer));
                break;
            default:
                scene->commands.push_back(sharedWriter.Write(word, value, context));
                break;
        }
    }
    return scene;
}

ExportResult MMSceneBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string&, YAML::Node&, std::string*) {
    auto scene = std::static_pointer_cast<MMSceneData>(raw);
    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::OoTRoom, 0);
    writer.Write(static_cast<uint32_t>(scene->commands.size()));
    for (auto& command : scene->commands) {
        writer.Write(command.cmdID);
        if (!command.data.empty()) {
            writer.Write(reinterpret_cast<char*>(command.data.data()), command.data.size());
        }
    }
    writer.Finish(write);
    return std::nullopt;
}

} // namespace MM
