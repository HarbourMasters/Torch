#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <vector>
#include <string>

namespace OoT {

// Must match SOH::SkeletonType enum values
enum class OoTSkeletonType : uint8_t {
    Normal = 0,
    Flex = 1,
    Curve = 2,
};

// Must match SOH::LimbType enum values
enum class OoTLimbType : uint8_t {
    Invalid = 0,
    Standard = 1,
    LOD = 2,
    Skin = 3,
    Curve = 4,
    Legacy = 5,
};

// Must match ZLimbSkinType enum values
enum class OoTLimbSkinType : int32_t {
    SkinType_Null = 0,
    SkinType_Animated = 4,
    SkinType_Normal = 11,
};

struct OoTSkinVertex {
    uint16_t index;
    int16_t s, t;
    int8_t normX, normY, normZ;
    uint8_t alpha;
};

struct OoTSkinTransformation {
    uint8_t limbIndex;
    int16_t x, y, z;
    uint8_t scale;
};

struct OoTSkinLimbModif {
    uint16_t unk_4;
    std::vector<OoTSkinVertex> skinVertices;
    std::vector<OoTSkinTransformation> limbTransformations;
};

struct OoTSkinAnimatedLimbData {
    uint16_t totalVtxCount;
    std::vector<OoTSkinLimbModif> limbModifications;
    std::string dlist; // resolved path
};

// Parsed data for a single OoT limb
class OoTLimbData : public IParsedData {
public:
    OoTLimbType limbType;

    // Skin-specific fields
    OoTLimbSkinType skinSegmentType = OoTLimbSkinType::SkinType_Null;
    std::string skinDList;        // resolved path (Skin + SkinType_Normal)
    uint16_t skinVtxCnt = 0;
    OoTSkinAnimatedLimbData skinAnimData;

    // Legacy-specific fields
    float legTransX = 0, legTransY = 0, legTransZ = 0;
    uint16_t rotX = 0, rotY = 0, rotZ = 0;
    std::string childPtr;    // resolved path (Legacy only)
    std::string siblingPtr;  // resolved path (Legacy only)

    // Common fields for Standard/LOD/Skin/Curve
    std::string dListPtr;    // resolved path
    std::string dList2Ptr;   // resolved path (LOD/Curve only)
    int16_t transX = 0, transY = 0, transZ = 0;
    uint8_t childIndex = 0, siblingIndex = 0;
};

// Parsed data for an OoT skeleton
class OoTSkeletonData : public IParsedData {
public:
    OoTSkeletonType skelType;
    OoTLimbType limbType;
    uint8_t limbCount;
    uint8_t dListCount; // Flex only
    std::vector<std::string> limbPaths; // resolved paths to each limb
};

// Shared helpers
OoTLimbType ParseLimbType(const std::string& str);
OoTSkeletonType ParseSkeletonType(const std::string& str);
std::string ResolveGfxPointer(uint32_t ptr, const std::string& limbSymbol,
                              const std::string& suffix);

} // namespace OoT

#endif
