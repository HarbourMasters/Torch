#ifdef OOT_SUPPORT

#include "OoTSceneFactory.h"
#include "OoTSceneCommandWriter.h"
#include "OoTSceneUtils.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "factories/DisplayListFactory.h"
#include <set>

namespace OoT {

// Collect all known data offsets from scene commands for neighbor-based size inference.
// Mimics ZAPD's GetDeclarationSizeFromNeighbor(): the size of a variable-length
// list is determined by the distance to the next known data structure.
std::set<uint32_t> OoTSceneFactory::CollectKnownAddresses(const std::vector<std::pair<uint32_t, uint32_t>>& rawCmds,
                                                          std::vector<uint8_t>& buffer) {
    std::set<uint32_t> addrs;
    for (auto& [w0, w1] : rawCmds) {
        uint8_t id = (w0 >> 24) & 0xFF;
        uint8_t arg1 = (w0 >> 16) & 0xFF;
        uint32_t addr = w1;
        if (addr == 0 || !IS_SEGMENTED(addr)) continue;

        // Skip inline commands (data is in the command word itself, not a pointer)
        if (id == SetWind || id == SetTimeSettings || id == SetSkyboxModifier ||
            id == SetEchoSettings || id == SetSoundSettings || id == SetSkyboxSettings ||
            id == SetRoomBehavior || id == SetCameraSettings || id == SetSpecialObjects ||
            id == EndMarker) {
            continue;
        }

        uint32_t off = SEGMENT_OFFSET(addr);
        uint8_t seg = (addr >> 24) & 0xFF;
        addrs.insert(off);

        // For fixed-count commands, also add end address (start + count * entrySize)
        switch (id) {
            case SetStartPositionList: addrs.insert(off + arg1 * 16); break;
            case SetActorList:         addrs.insert(off + arg1 * 16); break;
            case SetTransitionActorList: addrs.insert(off + arg1 * 16); break;
            case SetObjectList:        addrs.insert(off + arg1 * 2); break;
            case SetLightList:         addrs.insert(off + arg1 * 14); break;
            case SetLightingSettings:  addrs.insert(off + arg1 * 22); break;
            case SetRoomList:          addrs.insert(off + arg1 * 8); break;
            default: break;
        }

        // For SetPathways, pre-scan the pathway entries to add their point data addresses.
        if (id == SetPathways) {
            auto pathPeek = ReadSubArray(buffer, addr, 256 * 8);
            for (uint32_t i = 0; i < 256; i++) {
                uint8_t np = pathPeek.ReadUByte();
                pathPeek.ReadUByte(); pathPeek.ReadUByte(); pathPeek.ReadUByte();
                uint32_t ptsAddr = pathPeek.ReadUInt32();
                if (ptsAddr == 0 || !IS_SEGMENTED(ptsAddr) || ((ptsAddr >> 24) & 0xFF) != seg) {
                    addrs.insert(off + i * 8);
                    break;
                }
                addrs.insert(SEGMENT_OFFSET(ptsAddr));
                addrs.insert(SEGMENT_OFFSET(ptsAddr) + np * 6);
            }
        }
    }
    addrs.insert(rawCmds.size() * 8);
    return addrs;
}

std::optional<std::shared_ptr<IParsedData>> OoTSceneFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0x10000);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto scene = std::make_shared<OoTSceneData>();

    // Read 8-byte scene commands until EndMarker
    std::vector<std::pair<uint32_t, uint32_t>> rawCmds;
    while (reader.GetBaseAddress() < segment.size - 7) {
        uint32_t w0 = reader.ReadUInt32();
        uint32_t w1 = reader.ReadUInt32();
        uint8_t cmdID = (w0 >> 24) & 0xFF;
        rawCmds.push_back({w0, w1});
        if (cmdID == EndMarker) break;
    }

    auto entryName = GetSafeNode<std::string>(node, "symbol");
    auto currentDir = Companion::Instance->GetCurrentDirectory();
    auto assetType = GetSafeNode<std::string>(node, "type");
    std::string baseName = node["base_name"] ? node["base_name"].as<std::string>() : entryName;
    bool isAltHeader = node["base_name"].IsDefined();

    auto knownAddrs = CollectKnownAddresses(rawCmds, buffer);

    std::vector<PendingAltHeader> pendingAltHeaders;
    SceneWriteContext ctx {
        .buffer = buffer,
        .knownAddrs = knownAddrs,
        .entryName = entryName,
        .baseName = baseName,
        .currentDir = currentDir,
        .assetType = assetType,
        .isAltHeader = isAltHeader,
        .pendingAltHeaders = pendingAltHeaders,
    };

    SceneCommandWriter writer;
    for (auto& [w0, w1] : rawCmds) {
        scene->commands.push_back(writer.Write(w0, w1, ctx));
    }

    // Process deferred alternate headers now that primary DLists are registered.
    for (auto& alt : pendingAltHeaders) {
        auto existing = Companion::Instance->GetNodeByAddr(alt.seg);
        if (!existing.has_value()) {
            auto savedVtx = DeferredVtx::IsDeferred()
                ? DeferredVtx::SaveAndClearPending()
                : std::vector<DeferredVtx::PendingVtx>{};
            bool wasDeferred = DeferredVtx::IsDeferred();

            YAML::Node altNode;
            altNode["type"] = assetType;
            altNode["offset"] = alt.seg;
            altNode["symbol"] = alt.symbol;
            altNode["base_name"] = entryName;
            try {
                Companion::Instance->AddAsset(altNode);
            } catch (const std::exception& e) {
                SPDLOG_WARN("Scene: Failed to create alternate header {}: {}", alt.symbol, e.what());
            }

            if (wasDeferred) {
                DeferredVtx::RestorePending(savedVtx);
            }
        }
    }

    return scene;
}

ExportResult OoTSceneBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node,
                                             std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto scene = std::static_pointer_cast<OoTSceneData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTRoom, 0);
    writer.Write(static_cast<uint32_t>(scene->commands.size()));

    for (auto& cmd : scene->commands) {
        writer.Write(cmd.cmdID);
        if (!cmd.data.empty()) {
            writer.Write(reinterpret_cast<char*>(cmd.data.data()), cmd.data.size());
        }
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
