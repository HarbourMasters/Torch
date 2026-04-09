#pragma once

#ifdef OOT_SUPPORT

#include "OoTSceneFactory.h"
#include "OoTSceneUtils.h"
#include <set>

namespace OoT {

// Context passed through scene command serialization.
struct SceneWriteContext {
    std::vector<uint8_t>& buffer;
    const std::set<uint32_t>& knownAddrs;
    const std::string& entryName;
    const std::string& baseName;
    const std::string& currentDir;
    const std::string& assetType;
    bool isAltHeader;
    std::vector<PendingAltHeader>& pendingAltHeaders;
};

class SceneCommandWriter {
public:
    SceneCommand Write(uint32_t w0, uint32_t w1, SceneWriteContext& ctx);

private:
    void WriteSetWind(LUS::BinaryWriter& w, uint32_t w0, uint32_t w1);
    void WriteSetTimeSettings(LUS::BinaryWriter& w, uint32_t w1);
    void WriteSetSkyboxModifier(LUS::BinaryWriter& w, uint32_t w1);
    void WriteSetEchoSettings(LUS::BinaryWriter& w, uint32_t w1);
    void WriteSetSoundSettings(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t w1);
    void WriteSetSkyboxSettings(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t w1);
    void WriteSetRoomBehavior(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2);
    void WriteSetCameraSettings(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2);
    void WriteSetSpecialObjects(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t w1);
    void WriteSetStartPositionList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetActorList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetTransitionActorList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetEntranceList(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetObjectList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetLightingSettings(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetLightList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetExitList(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetRoomList(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetCollisionHeader(LUS::BinaryWriter& w, uint32_t cmdArg2);
    void WriteSetMesh(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetCsCamera(LUS::BinaryWriter& w, uint8_t cmdArg1, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetPathways(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetCutscenes(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx);
    void WriteSetAlternateHeaders(LUS::BinaryWriter& w, uint32_t cmdArg2, SceneWriteContext& ctx);

    void CreateBackgroundCompanion(std::vector<uint8_t>& buffer, uint32_t source, const std::string& bgSymbol);
    std::string ResolveGfxPointer(uint32_t ptr, const std::string& symbol);
    std::string ResolveGfxWithAlias(uint32_t ptr, const std::string& symbol, const std::string& currentDir);
    uint32_t GetNeighborSize(const std::set<uint32_t>& knownAddrs, uint32_t segAddr, uint32_t entrySize);
};

} // namespace OoT

#endif
