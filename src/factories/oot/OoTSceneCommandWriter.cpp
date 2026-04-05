#ifdef OOT_SUPPORT

#include "OoTSceneCommandWriter.h"
#include "AliasManager.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "factories/DisplayListFactory.h"
#include <sstream>
#include <iomanip>

namespace OoT {

SceneCommand SceneCommandWriter::Write(uint32_t w0, uint32_t w1, SceneWriteContext& ctx) {
    uint8_t cmdID = (w0 >> 24) & 0xFF;
    uint8_t cmdArg1 = (w0 >> 16) & 0xFF;
    uint32_t cmdArg2 = w1;

    SceneCommand cmd;
    cmd.cmdID = cmdID;

    LUS::BinaryWriter cmdWriter;

    switch (cmdID) {
        case SetWind: {
            WriteSetWind(cmdWriter, w0, w1);
            break;
        }
        case SetTimeSettings: {
            WriteSetTimeSettings(cmdWriter, w1);
            break;
        }
        case SetSkyboxModifier: {
            WriteSetSkyboxModifier(cmdWriter, w1);
            break;
        }
        case SetEchoSettings: {
            WriteSetEchoSettings(cmdWriter, w1);
            break;
        }
        case SetSoundSettings: {
            WriteSetSoundSettings(cmdWriter, cmdArg1, w1);
            break;
        }
        case SetSkyboxSettings: {
            WriteSetSkyboxSettings(cmdWriter, cmdArg1, w1);
            break;
        }
        case SetRoomBehavior: {
            WriteSetRoomBehavior(cmdWriter, cmdArg1, cmdArg2);
            break;
        }
        case SetCameraSettings: {
            WriteSetCameraSettings(cmdWriter, cmdArg1, cmdArg2);
            break;
        }
        case SetSpecialObjects: {
            WriteSetSpecialObjects(cmdWriter, cmdArg1, w1);
            break;
        }
        case SetStartPositionList: {
            WriteSetStartPositionList(cmdWriter, cmdArg1, cmdArg2, ctx);
            break;
        }
        case SetActorList: {
            WriteSetActorList(cmdWriter, cmdArg1, cmdArg2, ctx);
            break;
        }
        case SetTransitionActorList: {
            WriteSetTransitionActorList(cmdWriter, cmdArg1, cmdArg2, ctx);
            break;
        }
        case SetEntranceList: {
            WriteSetEntranceList(cmdWriter, cmdArg2, ctx);
            break;
        }
        case SetObjectList: {
            WriteSetObjectList(cmdWriter, cmdArg1, cmdArg2, ctx);
            break;
        }
        case SetLightingSettings: {
            WriteSetLightingSettings(cmdWriter, cmdArg1, cmdArg2, ctx);
            break;
        }
        case SetLightList: {
            WriteSetLightList(cmdWriter, cmdArg1, cmdArg2, ctx);
            break;
        }
        case SetExitList: {
            WriteSetExitList(cmdWriter, cmdArg2, ctx);
            break;
        }
        case SetRoomList: {
            WriteSetRoomList(cmdWriter, cmdArg1, cmdArg2, ctx);
            break;
        }
        case SetCollisionHeader: {
            WriteSetCollisionHeader(cmdWriter, cmdArg2);
            break;
        }
        case SetMesh: {
            WriteSetMesh(cmdWriter, cmdArg2, ctx);
            break;
        }
        case SetCsCamera: {
            WriteSetCsCamera(cmdWriter, cmdArg1, cmdArg2, ctx);
            break;
        }
        case SetPathways: {
            WriteSetPathways(cmdWriter, cmdArg2, ctx);
            break;
        }
        case SetCutscenes: {
            WriteSetCutscenes(cmdWriter, cmdArg2, ctx);
            break;
        }
        case SetAlternateHeaders: {
            WriteSetAlternateHeaders(cmdWriter, cmdArg2, ctx);
            break;
        }
        case EndMarker: {
            break;
        }
        default: {
            SPDLOG_WARN("Scene: Unhandled command 0x{:02X}", cmdID);
            break;
        }
    }

    std::stringstream ss;
    cmdWriter.Finish(ss);
    std::string str = ss.str();
    cmd.data = std::vector<uint8_t>(str.begin(), str.end());
    return cmd;
}

void SceneCommandWriter::WriteSetStartPositionList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2,
                                                    SceneWriteContext& ctx) {
    uint32_t count = cmdArg1;
    auto sub = ReadSubArray(ctx.buffer, cmdArg2, count * 16);
    w.Write(static_cast<uint32_t>(count));
    for (uint32_t i = 0; i < count; i++) {
        w.Write(sub.ReadInt16());  // actorNum
        w.Write(sub.ReadInt16());  // posX
        w.Write(sub.ReadInt16());  // posY
        w.Write(sub.ReadInt16());  // posZ
        w.Write(sub.ReadInt16());  // rotX
        w.Write(sub.ReadInt16());  // rotY
        w.Write(sub.ReadInt16());  // rotZ
        w.Write(sub.ReadInt16());  // params
    }
}

void SceneCommandWriter::WriteSetActorList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2,
                                           SceneWriteContext& ctx) {
    uint32_t count = cmdArg1;
    auto sub = ReadSubArray(ctx.buffer, cmdArg2, count * 16);
    w.Write(static_cast<uint32_t>(count));
    for (uint32_t i = 0; i < count; i++) {
        w.Write(sub.ReadInt16());  // actorNum
        w.Write(sub.ReadInt16());  // posX
        w.Write(sub.ReadInt16());  // posY
        w.Write(sub.ReadInt16());  // posZ
        w.Write(sub.ReadInt16());  // rotX
        w.Write(sub.ReadInt16());  // rotY
        w.Write(sub.ReadInt16());  // rotZ
        w.Write(sub.ReadInt16());  // params
    }

    // Create 0-byte ActorEntry companion file (matches OTRExporter behavior).
    if (cmdArg2 != 0 && count > 0) {
        uint32_t actorOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(cmdArg2));
        std::string actorSymbol = MakeAssetName(ctx.baseName, "ActorEntry", actorOffset);
        Companion::Instance->RegisterCompanionFile(actorSymbol, std::vector<char>{});
    }
}

void SceneCommandWriter::WriteSetWind(LUS::BinaryWriter& w, uint32_t w0, uint32_t w1) {
    w.Write(static_cast<uint8_t>((w1 >> 24) & 0xFF)); // windWest
    w.Write(static_cast<uint8_t>((w1 >> 16) & 0xFF)); // windVertical
    w.Write(static_cast<uint8_t>((w1 >> 8) & 0xFF));  // windSouth
    w.Write(static_cast<uint8_t>(w1 & 0xFF));         // clothFlappingStrength
}

void SceneCommandWriter::WriteSetTimeSettings(LUS::BinaryWriter& w, uint32_t w1) {
    w.Write(static_cast<uint8_t>((w1 >> 24) & 0xFF)); // hour
    w.Write(static_cast<uint8_t>((w1 >> 16) & 0xFF)); // min
    w.Write(static_cast<uint8_t>((w1 >> 8) & 0xFF));  // unk
}

void SceneCommandWriter::WriteSetSkyboxModifier(LUS::BinaryWriter& w, uint32_t w1) {
    w.Write(static_cast<uint8_t>((w1 >> 24) & 0xFF)); // disableSky
    w.Write(static_cast<uint8_t>((w1 >> 16) & 0xFF)); // disableSunMoon
}

void SceneCommandWriter::WriteSetEchoSettings(LUS::BinaryWriter& w, uint32_t w1) {
    w.Write(static_cast<uint8_t>(w1 & 0xFF)); // echo
}

void SceneCommandWriter::WriteSetSoundSettings(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t w1) {
    w.Write(static_cast<uint8_t>(cmdArg1));           // reverb
    w.Write(static_cast<uint8_t>((w1 >> 8) & 0xFF));  // nightTimeSFX
    w.Write(static_cast<uint8_t>(w1 & 0xFF));          // musicSequence
}

void SceneCommandWriter::WriteSetSkyboxSettings(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t w1) {
    w.Write(static_cast<uint8_t>(cmdArg1));            // unk1
    w.Write(static_cast<uint8_t>((w1 >> 24) & 0xFF)); // skyboxNumber
    w.Write(static_cast<uint8_t>((w1 >> 16) & 0xFF)); // cloudsType
    w.Write(static_cast<uint8_t>((w1 >> 8) & 0xFF));  // isIndoors
}

void SceneCommandWriter::WriteSetRoomBehavior(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2) {
    w.Write(static_cast<uint8_t>(cmdArg1)); // gameplayFlags
    w.Write(cmdArg2);                        // gameplayFlags2
}

void SceneCommandWriter::WriteSetCameraSettings(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2) {
    w.Write(static_cast<uint8_t>(cmdArg1)); // cameraMovement
    w.Write(cmdArg2);                        // mapHighlight
}

void SceneCommandWriter::WriteSetSpecialObjects(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t w1) {
    w.Write(static_cast<uint8_t>(cmdArg1));        // elfMessage
    w.Write(static_cast<uint16_t>(w1 & 0xFFFF));  // globalObject
}

void SceneCommandWriter::WriteSetTransitionActorList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2,
                                                     SceneWriteContext& ctx) {
    uint32_t count = cmdArg1;
    auto sub = ReadSubArray(ctx.buffer, cmdArg2, count * 16);
    w.Write(static_cast<uint32_t>(count));
    for (uint32_t i = 0; i < count; i++) {
        w.Write(sub.ReadInt8());   // frontObjectRoom
        w.Write(sub.ReadInt8());   // frontTransitionReaction
        w.Write(sub.ReadInt8());   // backObjectRoom
        w.Write(sub.ReadInt8());   // backTransitionReaction
        w.Write(sub.ReadInt16());  // actorNum
        w.Write(sub.ReadInt16());  // posX
        w.Write(sub.ReadInt16());  // posY
        w.Write(sub.ReadInt16());  // posZ
        w.Write(sub.ReadInt16());  // rotY
        w.Write(sub.ReadInt16());  // initVar
    }
}

void SceneCommandWriter::WriteSetEntranceList(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx) {
    uint32_t count = GetNeighborSize(ctx.knownAddrs, cmdArg2, 2);
    if (count == 0) count = 1;
    auto sub = ReadSubArray(ctx.buffer, cmdArg2, count * 2);
    w.Write(static_cast<uint32_t>(count));
    for (uint32_t i = 0; i < count; i++) {
        w.Write(sub.ReadUByte());  // startPositionIndex
        w.Write(sub.ReadUByte());  // roomToLoad
    }
}

void SceneCommandWriter::WriteSetObjectList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2,
                                            SceneWriteContext& ctx) {
    uint32_t count = cmdArg1;
    auto sub = ReadSubArray(ctx.buffer, cmdArg2, count * 2);
    w.Write(static_cast<uint32_t>(count));
    for (uint32_t i = 0; i < count; i++) {
        w.Write(sub.ReadUInt16());
    }
}

void SceneCommandWriter::WriteSetLightingSettings(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2,
                                                   SceneWriteContext& ctx) {
    uint32_t count = cmdArg1;
    auto sub = ReadSubArray(ctx.buffer, cmdArg2, count * 22);
    w.Write(static_cast<uint32_t>(count));
    for (uint32_t i = 0; i < count; i++) {
        for (int j = 0; j < 18; j++) {
            w.Write(sub.ReadUByte());
        }
        w.Write(sub.ReadUInt16()); // unk
        w.Write(sub.ReadUInt16()); // drawDistance
    }
}

void SceneCommandWriter::WriteSetLightList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2,
                                           SceneWriteContext& ctx) {
    uint32_t count = cmdArg1;
    auto sub = ReadSubArray(ctx.buffer, cmdArg2, count * 14);
    w.Write(static_cast<uint32_t>(count));
    for (uint32_t i = 0; i < count; i++) {
        w.Write(sub.ReadUByte());  // type
        w.Write(sub.ReadInt16());  // x
        w.Write(sub.ReadInt16());  // y
        w.Write(sub.ReadInt16());  // z
        w.Write(sub.ReadUByte());  // r
        w.Write(sub.ReadUByte());  // g
        w.Write(sub.ReadUByte());  // b
        w.Write(sub.ReadUByte());  // drawGlow
        w.Write(sub.ReadInt16());  // radius
    }
}

void SceneCommandWriter::WriteSetExitList(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx) {
    uint32_t count = GetNeighborSize(ctx.knownAddrs, cmdArg2, 2);
    if (count == 0) count = 1;
    auto sub = ReadSubArray(ctx.buffer, cmdArg2, count * 2);
    w.Write(static_cast<uint32_t>(count));
    for (uint32_t i = 0; i < count; i++) {
        w.Write(sub.ReadUInt16());
    }
}

void SceneCommandWriter::WriteSetRoomList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2,
                                          SceneWriteContext& ctx) {
    uint32_t count = cmdArg1;
    auto sub = ReadSubArray(ctx.buffer, cmdArg2, count * 8);
    w.Write(static_cast<uint32_t>(count));

    // Derive scene base name from current directory (e.g. "scenes/nonmq/bdan_scene" → "bdan")
    std::string sceneBase;
    auto lastSlash = ctx.currentDir.rfind('/');
    sceneBase = (lastSlash != std::string::npos) ? ctx.currentDir.substr(lastSlash + 1) : ctx.currentDir;
    std::string roomBase = sceneBase;
    if (roomBase.size() > 6 && roomBase.substr(roomBase.size() - 6) == "_scene") {
        roomBase = roomBase.substr(0, roomBase.size() - 6);
    }

    for (uint32_t i = 0; i < count; i++) {
        std::string roomName = ctx.currentDir + "/" + roomBase + "_room_" + std::to_string(i);
        w.Write(roomName);
        w.Write(sub.ReadUInt32()); // virtualAddressStart
        w.Write(sub.ReadUInt32()); // virtualAddressEnd
    }
}

void SceneCommandWriter::WriteSetCollisionHeader(LUS::BinaryWriter& w, uint32_t cmdArg2) {
    uint32_t colAddr = Companion::Instance->PatchVirtualAddr(cmdArg2);
    auto resolved = ResolvePointer(cmdArg2);
    if (resolved.empty()) {
        SPDLOG_WARN("Undeclared collision at 0x{:08X} — YAML enrichment incomplete", colAddr);
    }
    w.Write(resolved);
}

void SceneCommandWriter::WriteSetMesh(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx) {
    uint8_t meshData = 0;
    auto meshReader = ReadSubArray(ctx.buffer, cmdArg2, 12);
    uint8_t meshHeaderType = meshReader.ReadUByte();

    w.Write(meshData);
    w.Write(meshHeaderType);

    DeferredVtx::BeginDefer();

    if (meshHeaderType == 0 || meshHeaderType == 2) {
        uint32_t num = meshReader.ReadUByte();
        meshReader.ReadUInt16(); // padding
        uint32_t polyStart = meshReader.ReadUInt32();

        w.Write(static_cast<uint8_t>(num));

        uint32_t entrySize = (meshHeaderType == 2) ? 16 : 8;
        auto polyReader = ReadSubArray(ctx.buffer, polyStart, num * entrySize);

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

            w.Write(polyType);
            if (polyType == 2) {
                w.Write(cx);
                w.Write(cy);
                w.Write(cz);
                w.Write(unk_06);
            }

            std::string opaPath;
            if (opaAddr != 0) {
                uint32_t opaOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(opaAddr));
                std::string opaSymbol = MakeAssetName(ctx.entryName, "DL", opaOffset);
                opaPath = ResolveGfxWithAlias(opaAddr, opaSymbol, ctx.currentDir);
            }
            w.Write(opaPath.empty() ? std::string("") : opaPath);

            std::string xluPath;
            if (xluAddr != 0) {
                uint32_t xluOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(xluAddr));
                std::string xluSymbol = MakeAssetName(ctx.entryName, "DL", xluOffset);
                xluPath = ResolveGfxWithAlias(xluAddr, xluSymbol, ctx.currentDir);
            }
            w.Write(xluPath.empty() ? std::string("") : xluPath);
        }
    } else if (meshHeaderType == 1) {
        uint8_t format = meshReader.ReadUByte();
        meshReader.ReadUInt16(); // padding
        uint32_t polyDListAddr = meshReader.ReadUInt32();

        w.Write(format);

        auto pdlReader = ReadSubArray(ctx.buffer, polyDListAddr, 8);
        uint32_t opaAddr = pdlReader.ReadUInt32();
        uint32_t xluAddr = pdlReader.ReadUInt32();

        std::string opaPath, xluPath;
        if (opaAddr != 0) {
            uint32_t opaOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(opaAddr));
            std::string opaSymbol = MakeAssetName(ctx.entryName, "DL", opaOffset);
            opaPath = ResolveGfxWithAlias(opaAddr, opaSymbol, ctx.currentDir);
        }
        if (xluAddr != 0) {
            uint32_t xluOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(xluAddr));
            std::string xluSymbol = MakeAssetName(ctx.entryName, "DL", xluOffset);
            xluPath = ResolveGfxWithAlias(xluAddr, xluSymbol, ctx.currentDir);
        }
        w.Write(opaPath.empty() ? std::string("") : opaPath);
        w.Write(xluPath.empty() ? std::string("") : xluPath);

        if (format == 2) {
            auto mhReader = ReadSubArray(ctx.buffer, cmdArg2 + 0x08, 8);
            uint32_t count = mhReader.ReadUByte();
            mhReader.ReadUByte(); mhReader.ReadUByte(); mhReader.ReadUByte();
            uint32_t multiAddr = mhReader.ReadUInt32();

            w.Write(static_cast<uint32_t>(count));

            auto bgReader = ReadSubArray(ctx.buffer, multiAddr, count * 28);
            for (uint32_t i = 0; i < count; i++) {
                uint16_t unk_00 = bgReader.ReadUInt16();
                uint8_t id = bgReader.ReadUByte();
                bgReader.ReadUByte();
                uint32_t source = bgReader.ReadUInt32();
                uint32_t unk_0C = bgReader.ReadUInt32();
                uint32_t tlut = bgReader.ReadUInt32();
                uint16_t width = bgReader.ReadUInt16();
                uint16_t height = bgReader.ReadUInt16();
                uint8_t fmt = bgReader.ReadUByte();
                uint8_t siz = bgReader.ReadUByte();
                uint16_t mode0 = bgReader.ReadUInt16();
                uint16_t tlutCount = bgReader.ReadUInt16();
                bgReader.ReadUInt16();

                w.Write(unk_00);
                w.Write(id);

                uint32_t bgOffset = SEGMENT_OFFSET(Companion::Instance->PatchVirtualAddr(source));
                std::string bgSymbol = MakeAssetName(ctx.baseName, "Background", bgOffset);
                std::string bgPath = ResolvePointer(source);
                if (bgPath.empty()) {
                    bgPath = ctx.currentDir + "/" + bgSymbol;
                }
                w.Write(bgPath);

                w.Write(unk_0C); w.Write(tlut);
                w.Write(width); w.Write(height);
                w.Write(fmt); w.Write(siz);
                w.Write(mode0); w.Write(tlutCount);

                CreateBackgroundCompanion(ctx.buffer, source, bgSymbol);
            }
        } else {
            w.Write(static_cast<uint32_t>(1));
            w.Write(static_cast<uint16_t>(0)); // unk_00
            w.Write(static_cast<uint8_t>(0));  // id

            auto bgReader = ReadSubArray(ctx.buffer, cmdArg2 + 0x08, 24);
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
            std::string bgSymbol = MakeAssetName(ctx.baseName, "Background", bgOffset);
            std::string bgPath = ResolvePointer(source);
            if (bgPath.empty()) {
                bgPath = ctx.currentDir + "/" + bgSymbol;
            }
            w.Write(bgPath);

            w.Write(unk_0C); w.Write(tlut);
            w.Write(width); w.Write(height);
            w.Write(fmt); w.Write(siz);
            w.Write(mode0); w.Write(tlutCount);

            CreateBackgroundCompanion(ctx.buffer, source, bgSymbol);
        }

        if (polyDListAddr != 0) {
            w.Write(static_cast<uint8_t>(meshHeaderType));
            w.Write(opaPath.empty() ? std::string("") : opaPath);
            w.Write(xluPath.empty() ? std::string("") : xluPath);
        }
    }
}

void SceneCommandWriter::WriteSetCsCamera(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2,
                                          SceneWriteContext& ctx) {
    auto camReader = ReadSubArray(ctx.buffer, cmdArg2, 0x1000);
    uint32_t numCameras = cmdArg1;
    w.Write(numCameras);

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

    for (auto& c : cameras) {
        w.Write(c.type);
        w.Write(c.numPoints);

        auto pointReader = ReadSubArray(ctx.buffer, c.segOff, c.numPoints * 6);
        for (int16_t j = 0; j < c.numPoints; j++) {
            w.Write(pointReader.ReadInt16());
            w.Write(pointReader.ReadInt16());
            w.Write(pointReader.ReadInt16());
        }
    }
}

void SceneCommandWriter::WriteSetPathways(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx) {
    uint8_t pathSeg = (cmdArg2 >> 24) & 0xFF;
    uint32_t maxPaths = GetNeighborSize(ctx.knownAddrs, cmdArg2, 8);
    if (maxPaths == 0) maxPaths = 256;
    auto pathReader = ReadSubArray(ctx.buffer, cmdArg2, maxPaths * 8);
    std::vector<std::pair<uint8_t, uint32_t>> pathways;
    for (uint32_t i = 0; i < maxPaths; i++) {
        uint8_t np = pathReader.ReadUByte();
        pathReader.ReadUByte(); pathReader.ReadUByte(); pathReader.ReadUByte();
        uint32_t ptsAddr = pathReader.ReadUInt32();
        if (ptsAddr == 0 || !IS_SEGMENTED(ptsAddr) || ((ptsAddr >> 24) & 0xFF) != pathSeg) {
            break;
        }
        pathways.push_back({np, ptsAddr});
    }
    if (pathways.empty()) pathways.push_back({0, 0});

    auto existingPath = ResolvePointer(cmdArg2);
    bool hasPreExistingResource = !existingPath.empty();

    if (!hasPreExistingResource && ctx.isAltHeader && pathways.size() > 1) {
        pathways.erase(pathways.begin() + 1, pathways.end());
    }
    bool doubled = hasPreExistingResource && (pathways.size() > 1);
    uint32_t writeCount = doubled ? pathways.size() * 2 : pathways.size();

    w.Write(static_cast<uint32_t>(writeCount));

    uint32_t repeats = doubled ? 2 : 1;
    for (uint32_t r = 0; r < repeats; r++) {
        for (uint32_t i = 0; i < pathways.size(); i++) {
            auto [np, ptsAddr] = pathways[i];
            uint32_t pointOffset = SEGMENT_OFFSET(ptsAddr);
            std::string pathSymbol = MakeAssetName(ctx.baseName, "PathwayList", pointOffset);
            std::string pathPath = ctx.currentDir + "/" + pathSymbol;
            w.Write(pathPath);
        }
    }

    for (uint32_t i = 0; i < pathways.size(); i++) {
        auto [np, ptsAddr] = pathways[i];
        uint32_t pointOffset = SEGMENT_OFFSET(ptsAddr);
        std::string pathSymbol = MakeAssetName(ctx.baseName, "PathwayList", pointOffset);
        auto pathData = SerializePathways(ctx.buffer, pathways, writeCount, repeats);
        Companion::Instance->RegisterCompanionFile(pathSymbol, pathData);
    }
}

void SceneCommandWriter::WriteSetCutscenes(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx) {
    std::string csSymbol;
    uint32_t csAddr = Companion::Instance->PatchVirtualAddr(cmdArg2);
    auto resolved = ResolvePointer(cmdArg2);

    if (resolved.empty()) {
        uint32_t csOffset = SEGMENT_OFFSET(csAddr);
        csSymbol = MakeAssetName(ctx.entryName, "CutsceneData", csOffset);
        resolved = ctx.currentDir + "/" + csSymbol;
    } else {
        csSymbol = resolved.substr(resolved.rfind('/') + 1);
    }
    w.Write(resolved);

    auto csData = SerializeCutscene(ctx.buffer, cmdArg2);
    if (csData.empty()) {
        SPDLOG_WARN("Scene: Skipping cutscene {} due to parse failure", csSymbol);
    }
    Companion::Instance->RegisterCompanionFile(csSymbol, csData);
}

void SceneCommandWriter::WriteSetAlternateHeaders(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx) {
    uint32_t maxHeaders = GetNeighborSize(ctx.knownAddrs, cmdArg2, 4);
    if (maxHeaders == 0) maxHeaders = 16;
    auto hdrReader = ReadSubArray(ctx.buffer, cmdArg2, maxHeaders * 4);
    std::vector<uint32_t> headers;
    for (uint32_t i = 0; i < maxHeaders; i++) {
        uint32_t seg = hdrReader.ReadUInt32();
        headers.push_back(seg);
    }

    w.Write(static_cast<uint32_t>(headers.size()));
    for (auto seg : headers) {
        if (seg == 0) {
            w.Write(std::string(""));
        } else {
            uint32_t offset = SEGMENT_OFFSET(seg);
            std::string setSymbol = MakeAssetName(ctx.entryName, "Set", offset);
            std::string setPath = ctx.currentDir + "/" + setSymbol;
            w.Write(setPath);

            ctx.pendingAltHeaders.push_back({seg, setSymbol});
        }
    }
}

void SceneCommandWriter::CreateBackgroundCompanion(std::vector<uint8_t>& buffer, uint32_t source,
                                                   const std::string& bgSymbol) {
    if (source == 0) return;
    uint32_t bgDataSize = 320 * 240 * 2;
    auto bgDataReader = ReadSubArray(buffer, source, bgDataSize);
    LUS::BinaryWriter bgWriter;
    BaseExporter::WriteHeader(bgWriter, Torch::ResourceType::OoTBackground, 0);
    bgWriter.Write(static_cast<uint32_t>(bgDataSize));
    for (uint32_t b = 0; b < bgDataSize; b++) {
        bgWriter.Write(bgDataReader.ReadUByte());
    }
    std::stringstream bgSS;
    bgWriter.Finish(bgSS);
    std::string bgStr = bgSS.str();
    Companion::Instance->RegisterCompanionFile(
        bgSymbol, std::vector<char>(bgStr.begin(), bgStr.end()));
}

std::string SceneCommandWriter::ResolveGfxPointer(uint32_t ptr, const std::string& symbol) {
    if (ptr == 0) return "";
    ptr = Companion::Instance->PatchVirtualAddr(ptr);
    auto result = Companion::Instance->GetStringByAddr(ptr);
    if (result.has_value()) return result.value();

    SPDLOG_WARN("Scene: Could not resolve GFX pointer 0x{:08X} ({}) — YAML enrichment incomplete", ptr, symbol);
    return "";
}

std::string SceneCommandWriter::ResolveGfxWithAlias(uint32_t ptr, const std::string& symbol,
                                                    const std::string& currentDir) {
    std::string path = ResolveGfxPointer(ptr, symbol);
    if (path.empty()) return "";
    std::string expectedPath = currentDir + "/" + symbol;
    if (path != expectedPath) {
        AliasManager::Instance->Register(path, expectedPath);
    }
    return path;
}

uint32_t SceneCommandWriter::GetNeighborSize(const std::set<uint32_t>& knownAddrs, uint32_t segAddr,
                                             uint32_t entrySize) {
    uint32_t addr = SEGMENT_OFFSET(segAddr);
    auto it = knownAddrs.upper_bound(addr);
    if (it != knownAddrs.end()) {
        return (*it - addr) / entrySize;
    }
    return 0;
}

} // namespace OoT

#endif
