#ifdef OOT_SUPPORT

#include "OoTSceneFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "factories/DisplayListFactory.h"
#include <iomanip>
#include <set>
#include <sstream>

namespace OoT {

// Scene command IDs (matching OoT's RoomCommand enum)
enum SceneCmdID : uint32_t {
    SetStartPositionList = 0x00,
    SetActorList = 0x01,
    SetCsCamera = 0x02,
    SetCollisionHeader = 0x03,
    SetRoomList = 0x04,
    SetWind = 0x05,
    SetEntranceList = 0x06,
    SetSpecialObjects = 0x07,
    SetRoomBehavior = 0x08,
    // 0x09 = Unused
    SetMesh = 0x0A,
    SetObjectList = 0x0B,
    SetLightList = 0x0C,
    SetPathways = 0x0D,
    SetTransitionActorList = 0x0E,
    SetLightingSettings = 0x0F,
    SetTimeSettings = 0x10,
    SetSkyboxSettings = 0x11,
    SetSkyboxModifier = 0x12,
    SetExitList = 0x13,
    EndMarker = 0x14,
    SetSoundSettings = 0x15,
    SetEchoSettings = 0x16,
    SetCutscenes = 0x17,
    SetAlternateHeaders = 0x18,
    SetCameraSettings = 0x19,
};

// Helper to read a sub-array from ROM given a segmented pointer
static LUS::BinaryReader ReadSubArray(std::vector<uint8_t>& buffer, uint32_t segAddr, uint32_t size) {
    YAML::Node node;
    node["offset"] = Companion::Instance->PatchVirtualAddr(segAddr);
    auto raw = Decompressor::AutoDecode(node, buffer, size);
    LUS::BinaryReader reader(raw.segment.data, raw.segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    return reader;
}

// Helper: resolve a segmented pointer to an O2R asset path string
static std::string ResolvePointer(uint32_t ptr) {
    if (ptr == 0) return "";
    ptr = Companion::Instance->PatchVirtualAddr(ptr);
    auto result = Companion::Instance->GetStringByAddr(ptr);
    if (result.has_value()) return result.value();
    return "";
}

// Helper: build a scene-relative asset name from offset
static std::string MakeAssetName(const std::string& baseName, const std::string& suffix, uint32_t offset) {
    std::ostringstream ss;
    ss << baseName << suffix << "_" << std::uppercase << std::hex
       << std::setfill('0') << std::setw(6) << offset;
    return ss.str();
}

// Determine scene prefix for DList paths (matching OTRExporter GetParentFolderName).
// For rooms, the parent folder is the scene name, e.g. bdan_room_0 → bdan_scene.
static std::string GetSceneFolder(const std::string& currentDir) {
    // currentDir is like "scenes/nonmq/bdan_scene" or already correct
    return currentDir;
}

// Resolve a DList pointer, creating the GFX asset via AddAsset when not found.
static std::string ResolveGfxPointer(uint32_t ptr, const std::string& symbol, std::vector<uint8_t>& buffer) {
    if (ptr == 0) return "";
    ptr = Companion::Instance->PatchVirtualAddr(ptr);
    auto result = Companion::Instance->GetStringByAddr(ptr);
    if (result.has_value()) return result.value();

    // Auto-discover DList
    if (IS_SEGMENTED(ptr)) {
        auto seg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(ptr));
        if (seg.has_value()) {
            YAML::Node gfxNode;
            gfxNode["type"] = "GFX";
            gfxNode["offset"] = ptr;
            gfxNode["symbol"] = symbol;
            try {
                Companion::Instance->AddAsset(gfxNode);
                auto resolved = Companion::Instance->GetStringByAddr(ptr);
                if (resolved.has_value()) return resolved.value();
            } catch (...) {
                SPDLOG_WARN("Scene: Failed to create GFX asset {} at 0x{:08X}", symbol, ptr);
            }
        }
    }
    return "";
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

    // Alternate headers are deferred until after all primary commands (incl. SetMesh)
    // so that primary DLists are registered first and alt headers reuse their names.
    struct PendingAltHeader { uint32_t seg; std::string symbol; };
    std::vector<PendingAltHeader> pendingAltHeaders;

    // Collect all known data addresses from commands for neighbor-based size inference.
    // This mimics ZAPD's GetDeclarationSizeFromNeighbor(): the size of a variable-length
    // list is determined by the distance to the next known data structure.
    // For commands with known counts (from cmdArg1), we add BOTH start and end addresses
    // to create tight boundaries around all data arrays.
    std::set<uint32_t> knownAddrs;
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
        knownAddrs.insert(off);

        // For fixed-count commands, also add end address (start + count * entrySize)
        switch (id) {
            case SetStartPositionList: knownAddrs.insert(off + arg1 * 16); break;
            case SetActorList:         knownAddrs.insert(off + arg1 * 16); break;
            case SetTransitionActorList: knownAddrs.insert(off + arg1 * 16); break;
            case SetObjectList:        knownAddrs.insert(off + arg1 * 2); break;
            case SetLightList:         knownAddrs.insert(off + arg1 * 14); break;
            case SetLightingSettings:  knownAddrs.insert(off + arg1 * 22); break;
            case SetRoomList:          knownAddrs.insert(off + arg1 * 8); break;
            default: break;
        }

        // For SetPathways, pre-scan the pathway entries to add their point data addresses.
        // Only add entries with valid segment pointers (same segment as the list itself).
        // Stop at the first entry whose pointer is not a valid same-segment address.
        if (id == SetPathways) {
            auto pathPeek = ReadSubArray(buffer, addr, 256 * 8);
            for (uint32_t i = 0; i < 256; i++) {
                uint8_t np = pathPeek.ReadUByte();
                pathPeek.ReadUByte(); pathPeek.ReadUByte(); pathPeek.ReadUByte();
                uint32_t ptsAddr = pathPeek.ReadUInt32();
                if (ptsAddr == 0 || !IS_SEGMENTED(ptsAddr) || ((ptsAddr >> 24) & 0xFF) != seg) {
                    // End of valid pathway entries — mark the boundary at the first invalid entry
                    knownAddrs.insert(off + i * 8);
                    break;
                }
                knownAddrs.insert(SEGMENT_OFFSET(ptsAddr));
                // Also add the end of the point data
                knownAddrs.insert(SEGMENT_OFFSET(ptsAddr) + np * 6);
            }
        }
    }
    // Also add the end of the scene command table itself
    knownAddrs.insert(rawCmds.size() * 8);

    // Helper to infer list size from neighbor distance
    auto getNeighborSize = [&knownAddrs](uint32_t segAddr, uint32_t entrySize) -> uint32_t {
        uint32_t addr = SEGMENT_OFFSET(segAddr);
        auto it = knownAddrs.upper_bound(addr);
        if (it != knownAddrs.end()) {
            return (*it - addr) / entrySize;
        }
        return 0;
    };

    // Second pass: serialize each command's data matching RoomExporter.cpp format
    for (auto& [w0, w1] : rawCmds) {
        uint8_t cmdID = (w0 >> 24) & 0xFF;
        uint8_t cmdArg1 = (w0 >> 16) & 0xFF;
        uint32_t cmdArg2 = w1;

        SceneCommand cmd;
        cmd.cmdID = cmdID;

        // Use a local BinaryWriter to serialize the command body
        LUS::BinaryWriter cmdWriter;

        switch (cmdID) {
        case SetWind: {
            // 4 bytes inline from w0 and w1
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 24) & 0xFF)); // windWest
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 16) & 0xFF)); // windVertical
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 8) & 0xFF));  // windSouth
            cmdWriter.Write(static_cast<uint8_t>(w1 & 0xFF));         // clothFlappingStrength
            break;
        }
        case SetTimeSettings: {
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 24) & 0xFF)); // hour
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 16) & 0xFF)); // min
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 8) & 0xFF));  // unk
            break;
        }
        case SetSkyboxModifier: {
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 24) & 0xFF)); // disableSky
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 16) & 0xFF)); // disableSunMoon
            break;
        }
        case SetEchoSettings: {
            cmdWriter.Write(static_cast<uint8_t>(w1 & 0xFF)); // echo (byte 7)
            break;
        }
        case SetSoundSettings: {
            cmdWriter.Write(static_cast<uint8_t>(cmdArg1));                    // reverb (byte 1)
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 8) & 0xFF));          // nightTimeSFX
            cmdWriter.Write(static_cast<uint8_t>(w1 & 0xFF));                 // musicSequence
            break;
        }
        case SetSkyboxSettings: {
            cmdWriter.Write(static_cast<uint8_t>(cmdArg1));                    // unk1 (byte 1)
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 24) & 0xFF));         // skyboxNumber
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 16) & 0xFF));         // cloudsType
            cmdWriter.Write(static_cast<uint8_t>((w1 >> 8) & 0xFF));          // isIndoors
            break;
        }
        case SetRoomBehavior: {
            cmdWriter.Write(static_cast<uint8_t>(cmdArg1));  // gameplayFlags (byte 1)
            cmdWriter.Write(cmdArg2);                         // gameplayFlags2
            break;
        }
        case SetCameraSettings: {
            cmdWriter.Write(static_cast<uint8_t>(cmdArg1));  // cameraMovement (byte 1)
            cmdWriter.Write(cmdArg2);                         // mapHighlight
            break;
        }
        case SetSpecialObjects: {
            cmdWriter.Write(static_cast<uint8_t>(cmdArg1));                    // elfMessage
            cmdWriter.Write(static_cast<uint16_t>(w1 & 0xFFFF));              // globalObject
            break;
        }
        case EndMarker: {
            // No data
            break;
        }
        case SetStartPositionList:
        case SetActorList: {
            uint32_t count = cmdArg1;
            auto sub = ReadSubArray(buffer, cmdArg2, count * 16);
            cmdWriter.Write(static_cast<uint32_t>(count));
            for (uint32_t i = 0; i < count; i++) {
                cmdWriter.Write(sub.ReadInt16());  // actorNum
                cmdWriter.Write(sub.ReadInt16());  // posX
                cmdWriter.Write(sub.ReadInt16());  // posY
                cmdWriter.Write(sub.ReadInt16());  // posZ
                cmdWriter.Write(sub.ReadInt16());  // rotX
                cmdWriter.Write(sub.ReadInt16());  // rotY
                cmdWriter.Write(sub.ReadInt16());  // rotZ
                cmdWriter.Write(sub.ReadInt16());  // params
            }
            break;
        }
        case SetTransitionActorList: {
            uint32_t count = cmdArg1;
            auto sub = ReadSubArray(buffer, cmdArg2, count * 16);
            cmdWriter.Write(static_cast<uint32_t>(count));
            for (uint32_t i = 0; i < count; i++) {
                cmdWriter.Write(sub.ReadInt8());   // frontObjectRoom
                cmdWriter.Write(sub.ReadInt8());   // frontTransitionReaction
                cmdWriter.Write(sub.ReadInt8());   // backObjectRoom
                cmdWriter.Write(sub.ReadInt8());   // backTransitionReaction
                cmdWriter.Write(sub.ReadInt16());  // actorNum
                cmdWriter.Write(sub.ReadInt16());  // posX
                cmdWriter.Write(sub.ReadInt16());  // posY
                cmdWriter.Write(sub.ReadInt16());  // posZ
                cmdWriter.Write(sub.ReadInt16());  // rotY
                cmdWriter.Write(sub.ReadInt16());  // initVar
            }
            break;
        }
        case SetEntranceList: {
            // ZAPD infers count from neighbor distance (GetDeclarationSizeFromNeighbor / 2)
            uint32_t count = getNeighborSize(cmdArg2, 2);
            if (count == 0) count = 1; // fallback
            auto sub = ReadSubArray(buffer, cmdArg2, count * 2);
            cmdWriter.Write(static_cast<uint32_t>(count));
            for (uint32_t i = 0; i < count; i++) {
                cmdWriter.Write(sub.ReadUByte());  // startPositionIndex
                cmdWriter.Write(sub.ReadUByte());  // roomToLoad
            }
            break;
        }
        case SetObjectList: {
            uint32_t count = cmdArg1;
            auto sub = ReadSubArray(buffer, cmdArg2, count * 2);
            cmdWriter.Write(static_cast<uint32_t>(count));
            for (uint32_t i = 0; i < count; i++) {
                cmdWriter.Write(sub.ReadUInt16());
            }
            break;
        }
        case SetLightingSettings: {
            uint32_t count = cmdArg1;
            auto sub = ReadSubArray(buffer, cmdArg2, count * 22);
            cmdWriter.Write(static_cast<uint32_t>(count));
            for (uint32_t i = 0; i < count; i++) {
                // 22 bytes per entry: 3 ambient + 3 diffuseA + 3 dirA + 3 diffuseB + 3 dirB + 3 fog + 2 unk+drawDist
                for (int j = 0; j < 18; j++) {
                    cmdWriter.Write(sub.ReadUByte());
                }
                cmdWriter.Write(sub.ReadUInt16()); // unk
                cmdWriter.Write(sub.ReadUInt16()); // drawDistance
            }
            break;
        }
        case SetLightList: {
            uint32_t count = cmdArg1;
            auto sub = ReadSubArray(buffer, cmdArg2, count * 14);
            cmdWriter.Write(static_cast<uint32_t>(count));
            for (uint32_t i = 0; i < count; i++) {
                cmdWriter.Write(sub.ReadUByte());  // type
                cmdWriter.Write(sub.ReadInt16());  // x
                cmdWriter.Write(sub.ReadInt16());  // y
                cmdWriter.Write(sub.ReadInt16());  // z
                cmdWriter.Write(sub.ReadUByte());  // r
                cmdWriter.Write(sub.ReadUByte());  // g
                cmdWriter.Write(sub.ReadUByte());  // b
                cmdWriter.Write(sub.ReadUByte());  // drawGlow
                cmdWriter.Write(sub.ReadInt16());  // radius
            }
            break;
        }
        case SetExitList: {
            // ZAPD infers count from neighbor distance (GetDeclarationSizeFromNeighbor / 2)
            uint32_t count = getNeighborSize(cmdArg2, 2);
            if (count == 0) count = 1; // fallback
            auto sub = ReadSubArray(buffer, cmdArg2, count * 2);
            cmdWriter.Write(static_cast<uint32_t>(count));
            for (uint32_t i = 0; i < count; i++) {
                cmdWriter.Write(sub.ReadUInt16());
            }
            break;
        }
        case SetRoomList: {
            uint32_t count = cmdArg1;
            auto sub = ReadSubArray(buffer, cmdArg2, count * 8);
            cmdWriter.Write(static_cast<uint32_t>(count));

            // Derive scene base name from current directory
            // currentDir is like "scenes/nonmq/bdan_scene"
            std::string sceneBase;
            auto lastSlash = currentDir.rfind('/');
            if (lastSlash != std::string::npos) {
                sceneBase = currentDir.substr(lastSlash + 1); // "bdan_scene"
            } else {
                sceneBase = currentDir;
            }
            // Strip "_scene" to get the base name for rooms
            std::string roomBase = sceneBase;
            if (roomBase.size() > 6 && roomBase.substr(roomBase.size() - 6) == "_scene") {
                roomBase = roomBase.substr(0, roomBase.size() - 6);
            }

            for (uint32_t i = 0; i < count; i++) {
                std::string roomName = currentDir + "/" + roomBase + "_room_" + std::to_string(i);
                cmdWriter.Write(roomName);
                cmdWriter.Write(sub.ReadUInt32()); // virtualAddressStart
                cmdWriter.Write(sub.ReadUInt32()); // virtualAddressEnd
            }
            break;
        }
        case SetCollisionHeader: {
            uint32_t colAddr = Companion::Instance->PatchVirtualAddr(cmdArg2);
            auto resolved = ResolvePointer(cmdArg2);
            if (resolved.empty()) {
                // Auto-discover collision
                uint32_t offset = SEGMENT_OFFSET(colAddr);
                std::string colSymbol = entryName + "CollisionHeader_" +
                    ([offset]{ std::ostringstream s; s << std::uppercase << std::hex << std::setfill('0') << std::setw(6) << offset; return s.str(); })();

                YAML::Node colNode;
                colNode["type"] = "OOT:COLLISION";
                colNode["offset"] = colAddr;
                colNode["symbol"] = colSymbol;
                try {
                    Companion::Instance->AddAsset(colNode);
                    resolved = ResolvePointer(cmdArg2);
                } catch (const std::exception& e) {
                    SPDLOG_WARN("Scene: Failed to create collision at 0x{:08X}: {}", colAddr, e.what());
                }
            }
            cmdWriter.Write(resolved);
            break;
        }
        case SetMesh: {
            uint8_t meshData = 0;  // matches ZAPDTR fix: SetMesh::data was uninitialized, now 0
            // Read mesh header from ROM
            auto meshReader = ReadSubArray(buffer, cmdArg2, 12);
            uint8_t meshHeaderType = meshReader.ReadUByte();

            cmdWriter.Write(meshData);
            cmdWriter.Write(meshHeaderType);

            // Defer VTX registration so auto-discovered VTX can be consolidated
            // after all DLists (and their children) are parsed.
            DeferredVtx::BeginDefer();

            if (meshHeaderType == 0 || meshHeaderType == 2) {
                uint32_t num = meshReader.ReadUByte();
                meshReader.ReadUInt16(); // padding
                uint32_t polyStart = meshReader.ReadUInt32();

                cmdWriter.Write(static_cast<uint8_t>(num));

                // Each RoomShapeDListsEntry is 8 bytes for type 0, 16 bytes for type 2
                uint32_t entrySize = (meshHeaderType == 2) ? 16 : 8;
                auto polyReader = ReadSubArray(buffer, polyStart, num * entrySize);

                for (uint32_t i = 0; i < num; i++) {
                    uint8_t polyType = 0;
                    int16_t cx = 0, cy = 0, cz = 0;
                    int16_t unk_06 = 0;

                    if (meshHeaderType == 2) {
                        polyType = 2;
                        cx = polyReader.ReadInt16();
                        cy = polyReader.ReadInt16();
                        cz = polyReader.ReadInt16();
                        unk_06 = polyReader.ReadInt16();
                    }

                    uint32_t opaAddr = polyReader.ReadUInt32();
                    uint32_t xluAddr = polyReader.ReadUInt32();

                    cmdWriter.Write(polyType);
                    if (polyType == 2) {
                        cmdWriter.Write(cx);
                        cmdWriter.Write(cy);
                        cmdWriter.Write(cz);
                        cmdWriter.Write(unk_06);
                    }

                    // Resolve opa DList
                    std::string opaPath;
                    if (opaAddr != 0) {
                        uint32_t opaOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(opaAddr));
                        std::string opaSymbol = MakeAssetName(entryName, "DL", opaOffset);
                        opaPath = ResolveGfxPointer(opaAddr, opaSymbol, buffer);
                    }
                    cmdWriter.Write(opaPath.empty() ? std::string("") : opaPath);

                    // Resolve xlu DList
                    std::string xluPath;
                    if (xluAddr != 0) {
                        uint32_t xluOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(xluAddr));
                        std::string xluSymbol = MakeAssetName(entryName, "DL", xluOffset);
                        xluPath = ResolveGfxPointer(xluAddr, xluSymbol, buffer);
                    }
                    cmdWriter.Write(xluPath.empty() ? std::string("") : xluPath);
                }
            } else if (meshHeaderType == 1) {
                uint8_t format = meshReader.ReadUByte();
                meshReader.ReadUInt16(); // padding
                uint32_t polyDListAddr = meshReader.ReadUInt32();

                cmdWriter.Write(format);

                // Read the PolygonDList entry pointed to by dlist field
                auto pdlReader = ReadSubArray(buffer, polyDListAddr, 8);
                uint32_t opaAddr = pdlReader.ReadUInt32();
                uint32_t xluAddr = pdlReader.ReadUInt32();

                // Resolve opa/xlu DLists
                std::string opaPath, xluPath;
                if (opaAddr != 0) {
                    uint32_t opaOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(opaAddr));
                    std::string opaSymbol = MakeAssetName(entryName, "DL", opaOffset);
                    opaPath = ResolveGfxPointer(opaAddr, opaSymbol, buffer);
                }
                if (xluAddr != 0) {
                    uint32_t xluOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(xluAddr));
                    std::string xluSymbol = MakeAssetName(entryName, "DL", xluOffset);
                    xluPath = ResolveGfxPointer(xluAddr, xluSymbol, buffer);
                }
                cmdWriter.Write(opaPath.empty() ? std::string("") : opaPath);
                cmdWriter.Write(xluPath.empty() ? std::string("") : xluPath);

                // Background images
                if (format == 2) {
                    // Multiple backgrounds: count at meshHeader+0x08, list ptr at meshHeader+0x0C
                    auto mhReader = ReadSubArray(buffer, cmdArg2 + 0x08, 8);
                    uint32_t count = mhReader.ReadUByte();
                    mhReader.ReadUByte(); mhReader.ReadUByte(); mhReader.ReadUByte(); // padding
                    uint32_t multiAddr = mhReader.ReadUInt32();

                    cmdWriter.Write(static_cast<uint32_t>(count));

                    // Each BgImage entry is 28 bytes (0x1C)
                    auto bgReader = ReadSubArray(buffer, multiAddr, count * 28);
                    for (uint32_t i = 0; i < count; i++) {
                        uint16_t unk_00 = bgReader.ReadUInt16();
                        uint8_t id = bgReader.ReadUByte();
                        bgReader.ReadUByte(); // pad
                        uint32_t source = bgReader.ReadUInt32();
                        uint32_t unk_0C = bgReader.ReadUInt32();
                        uint32_t tlut = bgReader.ReadUInt32();
                        uint16_t width = bgReader.ReadUInt16();
                        uint16_t height = bgReader.ReadUInt16();
                        uint8_t fmt = bgReader.ReadUByte();
                        uint8_t siz = bgReader.ReadUByte();
                        uint16_t mode0 = bgReader.ReadUInt16();
                        uint16_t tlutCount = bgReader.ReadUInt16();
                        bgReader.ReadUInt16(); // pad

                        cmdWriter.Write(unk_00);
                        cmdWriter.Write(id);

                        // Resolve background blob
                        uint32_t bgOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(source));
                        std::string bgSymbol = MakeAssetName(entryName, "Background", bgOffset);
                        std::string bgPath = ResolvePointer(source);
                        if (bgPath.empty()) {
                            bgPath = currentDir + "/" + bgSymbol;
                        }
                        cmdWriter.Write(bgPath);

                        cmdWriter.Write(unk_0C);
                        cmdWriter.Write(tlut);
                        cmdWriter.Write(width);
                        cmdWriter.Write(height);
                        cmdWriter.Write(fmt);
                        cmdWriter.Write(siz);
                        cmdWriter.Write(mode0);
                        cmdWriter.Write(tlutCount);
                    }
                } else {
                    // Single background (format 1): data is inline at meshHeader+0x08
                    // In ZAPD, unk_00 and id are uninitialized for the single (sub-struct) case
                    cmdWriter.Write(static_cast<uint32_t>(1));

                    cmdWriter.Write(static_cast<uint16_t>(0)); // unk_00 (uninitialized in ZAPD, 0)
                    cmdWriter.Write(static_cast<uint8_t>(0));  // id (uninitialized in ZAPD, 0)

                    // Read inline bg fields: source, unk_0C, tlut, w, h, fmt, siz, mode0, tlutCount
                    auto bgReader = ReadSubArray(buffer, cmdArg2 + 0x08, 24);
                    uint32_t source = bgReader.ReadUInt32();
                    uint32_t unk_0C = bgReader.ReadUInt32();
                    uint32_t tlut = bgReader.ReadUInt32();
                    uint16_t width = bgReader.ReadUInt16();
                    uint16_t height = bgReader.ReadUInt16();
                    uint8_t fmt = bgReader.ReadUByte();
                    uint8_t siz = bgReader.ReadUByte();
                    uint16_t mode0 = bgReader.ReadUInt16();
                    uint16_t tlutCount = bgReader.ReadUInt16();

                    uint32_t bgOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(source));
                    std::string bgSymbol = MakeAssetName(entryName, "Background", bgOffset);
                    std::string bgPath = ResolvePointer(source);
                    if (bgPath.empty()) {
                        bgPath = currentDir + "/" + bgSymbol;
                    }
                    cmdWriter.Write(bgPath);

                    cmdWriter.Write(unk_0C);
                    cmdWriter.Write(tlut);
                    cmdWriter.Write(width);
                    cmdWriter.Write(height);
                    cmdWriter.Write(fmt);
                    cmdWriter.Write(siz);
                    cmdWriter.Write(mode0);
                    cmdWriter.Write(tlutCount);
                }

                // Trailing WritePolyDList (matches OTRExporter: if poly->dlist != 0)
                if (polyDListAddr != 0) {
                    cmdWriter.Write(static_cast<uint8_t>(meshHeaderType)); // polyType = 1
                    cmdWriter.Write(opaPath.empty() ? std::string("") : opaPath);
                    cmdWriter.Write(xluPath.empty() ? std::string("") : xluPath);
                }
            }

            // Note: deferred VTX mode stays active for child DLists parsed later.
            // Each DList flushes its own VTX at the end of its parse() call.
            break;
        }
        case SetCsCamera: {
            // Read cameras array
            auto camReader = ReadSubArray(buffer, cmdArg2, 0x1000);
            uint32_t numCameras = cmdArg1;
            cmdWriter.Write(numCameras);

            // First camera's segmentOffset is the base for computing point indices
            uint32_t firstPointsAddr = 0;
            struct CamEntry { int16_t type; int16_t numPoints; uint32_t segOff; };
            std::vector<CamEntry> cameras;

            for (uint32_t i = 0; i < numCameras; i++) {
                CamEntry c;
                c.type = camReader.ReadInt16();
                c.numPoints = camReader.ReadInt16();
                c.segOff = camReader.ReadUInt32();
                if (i == 0) firstPointsAddr = c.segOff;
                cameras.push_back(c);
            }

            // Read all points referenced by cameras
            for (auto& c : cameras) {
                cmdWriter.Write(c.type);
                cmdWriter.Write(c.numPoints);

                uint32_t pointOffset = (SEGMENT_OFFSET(c.segOff) - SEGMENT_OFFSET(firstPointsAddr)) / 6;
                auto pointReader = ReadSubArray(buffer, c.segOff, c.numPoints * 6);
                for (int16_t j = 0; j < c.numPoints; j++) {
                    cmdWriter.Write(pointReader.ReadInt16()); // x
                    cmdWriter.Write(pointReader.ReadInt16()); // y
                    cmdWriter.Write(pointReader.ReadInt16()); // z
                }
            }
            break;
        }
        case SetPathways: {
            // Read pathway list entries until zero terminator (listAddr == 0).
            // Each entry is 8 bytes: u8 numPoints, pad[3], u32 pointsSegPtr.
            uint8_t pathSeg = (cmdArg2 >> 24) & 0xFF;
            auto pathReader = ReadSubArray(buffer, cmdArg2, 256 * 8);
            std::vector<std::pair<uint8_t, uint32_t>> pathways; // numPoints, pointsAddr
            for (uint32_t i = 0; i < 256; i++) {
                uint8_t np = pathReader.ReadUByte();
                pathReader.ReadUByte(); pathReader.ReadUByte(); pathReader.ReadUByte(); // pad
                uint32_t ptsAddr = pathReader.ReadUInt32();
                if (ptsAddr == 0 || !IS_SEGMENTED(ptsAddr) || ((ptsAddr >> 24) & 0xFF) != pathSeg) {
                    break;
                }
                pathways.push_back({np, ptsAddr});
            }
            if (pathways.empty()) pathways.push_back({0, 0}); // fallback

            // OTRExporter has a bug where pathways are doubled when a ZPath XML resource
            // exists at the same offset as the SetPathways command. This happens for all
            // scenes with >1 pathway. We must match this behavior for binary compatibility.
            bool doubled = (pathways.size() > 1);
            uint32_t writeCount = doubled ? pathways.size() * 2 : pathways.size();

            cmdWriter.Write(static_cast<uint32_t>(writeCount));

            // Write pathway references (doubled if needed: original list + repeat)
            uint32_t repeats = doubled ? 2 : 1;
            for (uint32_t r = 0; r < repeats; r++) {
                for (uint32_t i = 0; i < pathways.size(); i++) {
                    auto [np, ptsAddr] = pathways[i];
                    uint32_t pointOffset = SEGMENT_OFFSET(ptsAddr);
                    std::string pathSymbol = MakeAssetName(entryName, "PathwayList", pointOffset);
                    std::string pathPath = currentDir + "/" + pathSymbol;
                    cmdWriter.Write(pathPath);
                }
            }

            // Register companion files (once per unique pathway)
            for (uint32_t i = 0; i < pathways.size(); i++) {
                auto [np, ptsAddr] = pathways[i];
                uint32_t pointOffset = SEGMENT_OFFSET(ptsAddr);
                std::string pathSymbol = MakeAssetName(entryName, "PathwayList", pointOffset);

                // Build Path companion file: header + u32 numPathways + per-pathway data
                // Companion files also reflect the doubled count to match OTRExporter.
                LUS::BinaryWriter pathFileWriter;
                BaseExporter::WriteHeader(pathFileWriter, Torch::ResourceType::OoTPath, 0);
                pathFileWriter.Write(static_cast<uint32_t>(writeCount));

                for (uint32_t r2 = 0; r2 < repeats; r2++) {
                    for (uint32_t j = 0; j < pathways.size(); j++) {
                        auto [numPts, ptAddr] = pathways[j];
                        pathFileWriter.Write(static_cast<uint32_t>(numPts));
                        auto ptReader = ReadSubArray(buffer, ptAddr, numPts * 6);
                        for (uint8_t k = 0; k < numPts; k++) {
                            pathFileWriter.Write(ptReader.ReadInt16()); // x
                            pathFileWriter.Write(ptReader.ReadInt16()); // y
                            pathFileWriter.Write(ptReader.ReadInt16()); // z
                        }
                    }
                }

                std::stringstream pathSS;
                pathFileWriter.Finish(pathSS);
                std::string pathStr = pathSS.str();
                std::vector<char> pathData(pathStr.begin(), pathStr.end());
                Companion::Instance->RegisterCompanionFile(pathSymbol, pathData);
            }
            break;
        }
        case SetCutscenes: {
            // OoT: single cutscene pointer
            std::string csSymbol;
            uint32_t csAddr = Companion::Instance->PatchVirtualAddr(cmdArg2);
            auto resolved = ResolvePointer(cmdArg2);

            if (resolved.empty()) {
                uint32_t csOffset = SEGMENT_OFFSET(csAddr);
                csSymbol = MakeAssetName(entryName, "Cs", csOffset);
                resolved = currentDir + "/" + csSymbol;
            } else {
                csSymbol = resolved.substr(resolved.rfind('/') + 1);
            }
            cmdWriter.Write(resolved);

            // Build Cutscene companion file
            // Read raw cutscene data from ROM (uint32 words until CS_END)
            auto csReader = ReadSubArray(buffer, cmdArg2, 0x10000);
            LUS::BinaryWriter csFileWriter;
            BaseExporter::WriteHeader(csFileWriter, Torch::ResourceType::OoTCutscene, 0);

            // Placeholder for size (will be filled in)
            uint32_t sizePos = csFileWriter.GetStream()->GetLength();
            csFileWriter.Write(static_cast<uint32_t>(0));

            uint32_t startPos = csFileWriter.GetStream()->GetLength();

            // Copy raw uint32 words including CS_BEGIN
            bool foundEnd = false;
            for (uint32_t i = 0; i < 0x4000; i++) {
                uint32_t word = csReader.ReadUInt32();
                csFileWriter.Write(word);
                if (word == 0xFFFFFFFF) {
                    // CS_END marker - write the trailing 0
                    csFileWriter.Write(csReader.ReadUInt32());
                    foundEnd = true;
                    break;
                }
            }

            if (!foundEnd) {
                SPDLOG_WARN("Scene: Cutscene at 0x{:08X} didn't find CS_END", csAddr);
                csFileWriter.Write(static_cast<uint32_t>(0xFFFFFFFF));
                csFileWriter.Write(static_cast<uint32_t>(0));
            }

            // Fill in the size (in uint32 words)
            uint32_t endPos = csFileWriter.GetStream()->GetLength();
            uint32_t dataSize = (endPos - startPos) / 4;
            csFileWriter.Seek(sizePos, LUS::SeekOffsetType::Start);
            csFileWriter.Write(dataSize);
            csFileWriter.Seek(endPos, LUS::SeekOffsetType::Start);

            std::stringstream csSS;
            csFileWriter.Finish(csSS);
            std::string csStr = csSS.str();
            std::vector<char> csData(csStr.begin(), csStr.end());
            Companion::Instance->RegisterCompanionFile(csSymbol, csData);
            break;
        }
        case SetAlternateHeaders: {
            // ZAPD infers count from neighbor distance (GetDeclarationSizeFromNeighbor / 4)
            uint32_t maxHeaders = getNeighborSize(cmdArg2, 4);
            if (maxHeaders == 0) maxHeaders = 16; // fallback
            auto hdrReader = ReadSubArray(buffer, cmdArg2, maxHeaders * 4);
            std::vector<uint32_t> headers;
            for (uint32_t i = 0; i < maxHeaders; i++) {
                uint32_t seg = hdrReader.ReadUInt32();
                headers.push_back(seg);
            }

            cmdWriter.Write(static_cast<uint32_t>(headers.size()));
            for (auto seg : headers) {
                if (seg == 0) {
                    cmdWriter.Write(std::string(""));
                } else {
                    uint32_t offset = SEGMENT_OFFSET(seg);
                    std::string setSymbol = MakeAssetName(entryName, "Set", offset);
                    std::string setPath = currentDir + "/" + setSymbol;
                    cmdWriter.Write(setPath);

                    // Defer alternate header creation until after all primary commands
                    // are processed (especially SetMesh, which registers DLists).
                    // This prevents alt headers from creating DLists with Set_ names
                    // before the primary DLists are registered.
                    pendingAltHeaders.push_back({seg, setSymbol});
                }
            }
            break;
        }
        default: {
            SPDLOG_WARN("Scene: Unhandled command 0x{:02X}", cmdID);
            break;
        }
        }

        std::stringstream cmdSS;
        cmdWriter.Finish(cmdSS);
        std::string cmdStr = cmdSS.str();
        cmd.data = std::vector<uint8_t>(cmdStr.begin(), cmdStr.end());
        scene->commands.push_back(cmd);
    }

    // Process deferred alternate headers now that primary DLists are registered.
    // Save/restore DeferredVtx state so child processing doesn't corrupt ours.
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
