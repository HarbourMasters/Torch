#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

/**
 * BoneData: one bone joint in the animation skeleton.
 * Decomp: BKAnimation from model.h
 *
 * Structure Layout:
 *   Offset 0x00: unk0[3] (f32[3]) - X, Y, Z bone position offset from parent
 *   Offset 0x0C: bone_id (s16) - Bone index
 *   Offset 0x0E: mtx_id (s16) - Parent bone matrix ID
 */
typedef struct BoneData {
    float pos[3];
    uint16_t id;
    uint16_t parentId;
} BoneData;

/**
 * GeoCube: one cell of the collision spatial partition.
 * Decomp: BKCollisionGeo from model.h
 *
 * Structure Layout:
 *   Offset 0x00: start_tri_index (s16) - Index of first triangle in this cube
 *   Offset 0x02: tri_count (s16) - Number of triangles in this cube
 */
typedef struct GeoCube {
    uint16_t startTri;
    uint16_t triCount;
} GeoCube;

/**
 * CollisionTri: a single collision triangle.
 * Decomp: BKCollisionTri from model.h
 *
 * Structure Layout:
 *   Offset 0x00: unk0[3] (s16[3]) - Vertex indices (we name it vtxIds[3])
 *   Offset 0x06: unk6 (s16) - Additional flags/material ID
 *   Offset 0x08: flags (s32) - Surface type flags
 */
typedef struct CollisionTri {
    uint16_t vtxIds[3];
    uint16_t unk6;
    uint32_t flags;
} CollisionTri;

/**
 * Effect: a group of vertices tagged for some special rendering effect.
 */
typedef struct Effect {
    uint16_t dataInfo;                // packed effect type + params
    std::vector<uint16_t> vtxIndices; // the vertices this effect acts on
} Effect;

/**
 * AnimTexture: one animated-texture slot.
 * Decomp: AnimTexture from model.h
 *
 * Structure Layout (decomp field names):
 *   Offset 0x00: frame_size (s16) - Bytes per texture frame
 *   Offset 0x02: frame_cnt (s16) - Number of animation frames
 *   Offset 0x04: frame_rate (f32) - Animation speed (frames per second)
 */
typedef struct AnimTexture {
    uint16_t frameSize;
    uint16_t frameCount;
    float frameRate;
} AnimTexture;

/**
 * CollisionHeader: the grid parameters for collision lookups.
 * Decomp: BKCollisionList from model.h
 *
 * Structure Layout (decomp field names):
 *   Offset 0x00: unk0[3] (s16[3]) - min[X,Y,Z]
 *   Offset 0x06: unk6[3] (s16[3]) - max[X,Y,Z]
 *   Offset 0x0C: unkC (s16) - y_stride
 *   Offset 0x0E: unkE (s16) - z_stride
 *   Offset 0x10: unk10 (s16) - geo_cnt (we calculate from vector.size())
 *   Offset 0x12: unk12 (s16) - scale
 *   Offset 0x14: unk14 (s16) - tri_cnt (we calculate from vector.size())
 *
 * Grid Cell Calculation:
 *   cubeX = (worldX - minX) / geoCubeScale
 *   cubeY = (worldY - minY) / geoCubeScale
 *   cubeZ = (worldZ - minZ) / geoCubeScale
 *   cubeIndex = cubeX + cubeY * yStride + cubeZ * yStride * zStride
 */
typedef struct CollisionHeader {
    int16_t minIndex[3];
    int16_t maxIndex[3];
    uint16_t yStride;
    uint16_t zStride;
    uint16_t geoCubeScale;
} CollisionHeader;

/**
 * AnimationHeader: just the animation scaling factor.
 * Decomp: BKAnimationList from model.h
 *
 * Structure Layout:
 *   Offset 0x00: unk0 (f32) - Scaling multiplier for animation keyframes
 *   Offset 0x04: cnt_4 (s16) - Number of bones (we calculate from
 * vector.size())
 */
typedef struct AnimationHeader {
    float scalingFactor;
} AnimationHeader;

/**
 * VtxHeader: the bounding/position metadata at the front of a BKVertexList — the
 * first 24 bytes, ahead of the Vtx[] array. We write it into the binary so the
 * port can rebuild the BKVertexList header at runtime without the raw ROM segment.
 */
typedef struct VtxHeader {
    int16_t minCoord[3];
    int16_t maxCoord[3];
    int16_t centerCoord[3];
    int16_t localNorm;
    uint16_t count;
    int16_t globalNorm;
} VtxHeader;

/**
 * TexInfo: the per-texture metadata from the BKTextureList header.
 * tlutColors: 0x10 for CI4, 0x100 for CI8, 0 for non-paletted formats.
 */
typedef struct TexInfo {
    uint16_t type;
    uint8_t width;
    uint8_t height;
    uint16_t tlutColors;
    uint32_t textureDataOffset; // ROM offset, relative to the texture data base
} TexInfo;

/**
 * Unk14_0 / _1 / _2: hitbox / bone-space definitions, the entries of a
 * BKModelUnk14List. Sizes: _0 = 24 bytes, _1 = 16 bytes, _2 = 12 bytes.
 */
typedef struct Unk14_0 {
    int16_t scale1[3];
    int16_t scale2[3];
    int16_t pos[3];
    uint8_t rot[3];
    uint8_t unk15;
    int8_t animIndex;
    uint8_t pad;
} Unk14_0; // 24 bytes

typedef struct Unk14_1 {
    int16_t unk0;
    int16_t unk2;
    int16_t pos[3];
    uint8_t rot[3];
    uint8_t unkD;
    int8_t animIndex;
    uint8_t pad;
} Unk14_1; // 16 bytes

typedef struct Unk14_2 {
    int16_t unk0;
    int16_t unk2[3];
    uint8_t unk8;
    int8_t unk9;
    uint8_t pad[2];
} Unk14_2; // 12 bytes

/**
 * Unk20_0: one BKModelUnk20List entry. 14 bytes of data, padded to 16.
 */
typedef struct Unk20_0 {
    int16_t unk0[3];
    int16_t unk6[3];
    uint8_t unkC;
    uint8_t pad;
} Unk20_0; // 14 bytes raw, pad to 16

/**
 * Unk28_0: a variable-length BKModelUnk28 entry.
 */
typedef struct Unk28_0 {
    int16_t coord[3];
    int8_t animIndex;
    std::vector<int16_t> vtxList;
} Unk28_0;

class ModelData : public IParsedData {
  public:
    uint16_t mGeoType;
    uint16_t mTriCount;
    uint16_t mVertCount;

    // ── GEO layout ────────────────────────────────────────────────────────────
    bool mHasGeo = false;

    // ── Vertex list ───────────────────────────────────────────────────────────
    bool mHasVtx = false;
    VtxHeader mVtxHeader{};

    // ── Display lists ─────────────────────────────────────────────────────────
    bool mHasDL = false;
    uint32_t mDLCount = 0;             // total GFX command words over all sub-lists
    uint32_t mDLUnkInfo = 0;           // checksum, or whatever — from the GFX header
    uint32_t mGfxSubListCount = 0;     // how many _GFX_* sub-assets we made
    std::vector<uint32_t> mRawDLWords; // raw N64 DL words, as w0/w1 pairs

    // ── Texture list ──────────────────────────────────────────────────────────
    std::vector<TexInfo> mTexInfos;
    uint32_t mTexDataSize = 0;        // size of the raw texture area, for segment 2 allocation
    std::vector<uint8_t> mRawTexData; // the whole raw texture blob, animated frames and all

    // ── Animation data ────────────────────────────────────────────────────────
    bool mHasAnimation = false;
    AnimationHeader mAnimHeader{};
    std::vector<BoneData> mBones;

    // ── Collision data ────────────────────────────────────────────────────────
    bool mHasCollision = false;
    CollisionHeader mCollisionHeader{};
    std::vector<GeoCube> mGeoCubes;
    std::vector<CollisionTri> mCollisionTris;

    // ── Unk14 (hitbox) ────────────────────────────────────────────────────────
    bool mHasUnk14 = false;
    int16_t mUnk14Unk6 = 0;
    std::vector<Unk14_0> mUnk14Entries0;
    std::vector<Unk14_1> mUnk14Entries1;
    std::vector<Unk14_2> mUnk14Entries2;

    // ── Unk20 ─────────────────────────────────────────────────────────────────
    bool mHasUnk20 = false;
    std::vector<Unk20_0> mUnk20Entries;

    // ── Effects ───────────────────────────────────────────────────────────────
    std::vector<Effect> mEffects;

    // ── Unk28 ─────────────────────────────────────────────────────────────────
    bool mHasUnk28 = false;
    std::vector<Unk28_0> mUnk28Entries;

    // ── Animated textures ─────────────────────────────────────────────────────
    std::vector<AnimTexture> mAnimTextures;

    ModelData(uint16_t geoType, uint16_t triCount, uint16_t vertCount)
        : mGeoType(geoType), mTriCount(triCount), mVertCount(vertCount) {
    }
};

class ModelHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class ModelBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class ModelCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class ModelFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, ModelCodeExporter) REGISTER(Header, ModelHeaderExporter)
                     REGISTER(Binary, ModelBinaryExporter) };
    }

    bool HasModdedDependencies() override {
        return true;
    }
};
} // namespace BK64
