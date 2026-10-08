#pragma once

#include <factories/BaseFactory.h>
#include <vector>

namespace BK64 {

/**
 * NodeProp: spawn point, warp, trigger, or event marker - 20 bytes
 * Decomp: NodeProp from props.h
 *
 * These are the spatial triggers and spawn locations for a level. At load time
 * cubeList_fromFile (actor_cubebounds.c) turns each one into an ActorMarker.
 * The category field is what decides what it actually does:
 *   * 3 = Warp (touching it warps the player; actorId picks the warp)
 *   * 4 = Contact trigger (touching it runs the action actorId picks)
 *   * 6 = Actor spawn point (spawns an entity via markerActorTypeArray dispatch)
 *   * 7 = Enemy area (keeps enemies inside it)
 *   * 8 = Path point
 *   * 9 = Camera zone (switches to camera actorId while the player is inside)
 *   * 0xA = Collectible ID zone (numbers the jiggies, honeycombs and tokens inside it)
 * With bit0 set the record is a path waypoint instead, and every field after the
 * position is packed waypoint data.
 *
 * Flow: ROM → MapFactory parse() → NodeProp array → cubeList_fromFile →
 *       ActorMarker → actor spawn / event trigger via overlay callbacks
 *
 * Structure Layout:
 *   Offset 0x00: position[3] (s16[3]) - X, Y, Z world coordinates
 *   Offset 0x06: selector_or_radius:9, category:6, bit0:1 (u16)
 *   Offset 0x08: actorId (u16) - Actor/Warp/Event ID depending on category
 *   Offset 0x0A: markerId (u8), padB (u8)
 *   Offset 0x0C: yaw:9, scale:23 (u32)
 *   Offset 0x10: unk10_31:12, unk10_19:12, pad10_7:1, unk10_6:1, pad10_5:4,
 * unk10_0:2 (u32)
 */
typedef struct NodeProp {
    int16_t position[3];    // X, Y, Z world position (s16)
    uint16_t radius : 9;    // selector_or_radius: trigger/volume radius
    uint16_t bit6 : 6;      // category, see above
    uint16_t bit0 : 1;      // path waypoint
    uint16_t unk8;          // actorId — meaning depends on category
    uint8_t unkA;           // markerId: index into the ActorMarker lookup table
    uint8_t padB;           // padding
    uint32_t yaw : 9;       // degrees
    uint32_t scale : 23;    // percent for an actor spawn, 0 for its default size
    uint32_t unk10_31 : 12; // this record's number in a path chain
    uint32_t unk10_19 : 12; // the next record's number in the chain
    uint32_t pad10_7 : 1;   // padding
    uint32_t unk10_6 : 1;   // "initialized" flag, set at runtime
    uint32_t pad10_5 : 4;   // padding
    uint32_t unk10_0 : 2;   // param passed to func_803303B8
} NodeProp;

/**
 * ModelProp: Static 3D model (is_actor=0, is_3d=1) - 12 bytes
 * Decomp: model_prop_s from props.h
 *
 * Structure Layout:
 *   Offset 0x00: unk0 (u16) - modelId:12, pad0_19:4
 *   Offset 0x02: yaw (u8) - rotation Y-axis
 *   Offset 0x03: roll (u8) - rotation around local axis
 *   Offset 0x04: position[3] (s16[3]) - X, Y, Z world position
 *   Offset 0x0A: scale (u8)
 *   Offset 0x0B: flags (u8) - isModelProp:1, isActorProp:1, etc.
 *
 * Asset ID = (modelId & 0xFFF) + MODEL_ASSET_OFFSET (0x2D1)
 */
typedef struct ModelProp {
    uint16_t unk0;       // modelId:12, pad0_19:4
    uint8_t yaw;         // Y-axis rotation
    uint8_t roll;        // roll
    int16_t position[3]; // X, Y, Z world position
    uint8_t scale;       // scale
    uint8_t flags;       // discriminator (isModelProp=1, isActorProp=0)
} ModelProp;

/**
 * SpriteProp: 2D billboard sprite (is_actor=0, is_3d=0) - 12 bytes
 * Decomp: sprite_prop_s from props.h
 *
 * Complete Structure Layout (12 bytes):
 *   Offset 0x00-0x03: word0 (32-bit packed) - sprite appearance/rendering
 * parameters Offset 0x04-0x09: unk4[3] (s16[3]) - X, Y, Z world position Offset
 * 0x0A-0x0B: wordA (16-bit packed) - animation frame + discriminator flags
 *
 * Packed Field Layout (word0 - 32-bit big-endian at offset 0x00):
 *   Bits 31-20: spriteId (12 bits) → Asset ID = spriteId + SPRITE_ASSET_OFFSET
 * (0x572) Bit 19: unk0_19 (1 bit) Bits 18-16: rgb_remove_red (3 bits, 0-7 color
 * removal value) Bits 15-13: rgb_remove_green (3 bits, 0-7 color removal value)
 *   Bits 12-10: rgb_remove_blue (3 bits, 0-7 color removal value)
 *   Bits 9-2: scale (8 bits)
 *   Bit 1: isMirrored (1 bit, horizontal flip)
 *   Bit 0: pad0_0 (1 bit)
 *
 * Packed Field Layout (wordA - 16-bit big-endian at offset 0x0A):
 *   Bits 15-11: frame (5 bits, animation frame index)
 *   Bits 10-6: unk8_10 (5 bits)
 *   Bit 5: unk8_5 (1 bit)
 *   Bit 4: isNotFeatherEggOrNote (1 bit)
 *   Bit 3: unk8_3 (1 bit)
 *   Bit 2: isCollisionResolved (1 bit)
 *   Bit 1: isModelProp (1 bit, always 0 for sprites)
 *   Bit 0: isActorProp (1 bit, always 0 for sprites)
 */
typedef struct SpriteProp {
    uint32_t word0;
    int16_t unk4[3];
    uint16_t wordA;
} SpriteProp;

/**
 * ActorProp: Dynamic entity created at runtime (is_actor=1) - 12 bytes
 * Decomp: actor_prop_s from props.h
 *
 * Structure Layout:
 *   Offset 0x00: marker (ActorMarker* - 4 bytes, runtime pointer)
 *   Offset 0x04: position[3] (s16[3] - 6 bytes, X/Y/Z cache)
 *   Offset 0x0A: flags (u16 - 2 bytes) - frame:5, unk8_10:5, isMirrored:1,
 *                isNotFeatherEggOrNote:1, unk8_3:1, isCollisionResolved:1,
 *                isModelProp:1 (0), isActorProp:1 (1)
 *
 * Yes, ActorProps really are in ROM. The `marker` field (bytes 0-3) is always
 * NULL there and only gets filled in when the engine spawns the actor. The
 * position and flags fields, though, are genuine ROM data.
 */
typedef struct ActorProp {
    uint32_t marker;     // ActorMarker* — runtime only
    int16_t position[3]; // X, Y, Z position cache
    uint16_t flags;      // discriminator (isActorProp=1)
} ActorProp;

/**
 * Prop: union of ModelProp, SpriteProp, ActorProp (12 bytes)
 *
 * Which one it is comes from the flags at offset 0x0A, bits 0-1:
 *   isActorProp=1 → ActorProp (carries actor marker metadata)
 *   isActorProp=0, isModelProp=1 → ModelProp (static 3D geometry)
 *   isActorProp=0, isModelProp=0 → SpriteProp (2D billboard sprite)
 *
 * All three are 12 bytes and live in ROM; only the flags tell them apart.
 */
typedef union Prop {
    ModelProp model;
    SpriteProp sprite;
    ActorProp actor;
    struct {
        uint32_t pad0;
        int16_t unk4[3];
        uint16_t padA_15 : 10;
        uint16_t unkA_5 : 1;
        uint16_t unkA_4 : 1;
        uint16_t unkA_3 : 1;
        uint16_t unkA_2 : 1;
        uint16_t is_3d : 1;
        uint16_t is_actor : 1;
    };
    uint8_t raw[12];
} Prop;

/**
 * CubeData: one cell of the 32×32×32 spatial partition grid
 *
 * x/y/z are 5-bit grid coords (0-31).
 * unk0_4 splits the NodeProps: [0..unk0_4) are regular spawns,
 * [unk0_4..prop1Cnt) are events.
 */
typedef struct CubeData {
    int32_t x : 5;
    int32_t y : 5;
    int32_t z : 5;
    uint32_t prop1Cnt : 6;
    uint32_t prop2Cnt : 6;
    uint32_t unk0_4 : 5; // split point: [0..unk0_4)=spawns, [unk0_4..prop1Cnt)=events
    std::vector<NodeProp> nodeProps;
    std::vector<Prop> props;
} CubeData;

/**
 * CameraNodeType1: pivot camera, stays put and turns to follow the player
 * Decomp: PivotCameraNode in camera.h
 * Size: 44 bytes (11 floats + 1 s32)
 */
typedef struct CameraNodeType1 {
    float position[3];     // camera position
    float horizontalSpeed; // horizontal speed
    float verticalSpeed;   // vertical speed
    float rotation;        // rotation speed
    float accelaration;    // acceleration
    float pitchYawRoll[3]; // the point it looks from while the player is nearly underneath it
    int32_t unknownFlag;   // unknown; tested against 1, 2, 4
} CameraNodeType1;

/**
 * CameraNodeType2: static camera, only cutscenes and scripts cut to it
 * Decomp: StaticCameraNode in camera.h
 * Size: 24 bytes (6 floats)
 */
typedef struct CameraNodeType2 {
    float position[3];     // camera position
    float pitchYawRoll[3]; // orientation (pitch, yaw, roll)
} CameraNodeType2;

/**
 * CameraNodeType3: dolly camera, follows the player between its close and far distance
 * Decomp: ZoomCameraNode in camera.h
 * Size: 52 bytes (12 floats + 1 s32)
 */
typedef struct CameraNodeType3 {
    float position[3];     // camera position
    float horizontalSpeed; // horizontal speed
    float verticalSpeed;   // vertical speed
    float rotation;        // rotation speed
    float accelaration;    // acceleration
    float closeDistance;   // nearest it comes to the player
    float farDistance;     // farthest it drops back
    float pitchYawRoll[3]; // the point it looks from while the player is nearly underneath it
    int32_t unknownFlag;   // unknown
} CameraNodeType3;

/**
 * CameraNodeType4: zoom preset for the normal camera
 * Decomp: RandomCameraNode in camera.h
 * Size: 4 bytes (1 s32)
 */
typedef struct CameraNodeType4 {
    int32_t unknownFlag; // the preset
} CameraNodeType4;

/**
 * CameraNode: Camera path/behavior node
 * Decomp: CameraNode from camera.h
 * Format: marker 0x01 + index (s16) + type marker 0x02 + type (u8) +
 * type-specific data
 *
 * Camera types:
 *   0 = Empty
 *   1 = Pivot
 *   2 = Static
 *   3 = Dolly
 *   4 = Zoom preset
 */
typedef struct CameraNode {
    int16_t index; // node index
    uint8_t type;  // 0=Empty, 1=Pivot, 2=Static, 3=Dolly, 4=ZoomPreset
    union {
        CameraNodeType1 type1;
        CameraNodeType2 type2;
        CameraNodeType3 type3;
        CameraNodeType4 type4;
    } data;
} CameraNode;

/**
 * LightingVector: point light with a fade radius
 * Decomp: Lighting from gclights.c
 * Format: marker 0x01 + position (f32[3]) + fade_radii (f32[2]) + rgb (s32[3])
 */
typedef struct LightingVector {
    float position[3];  // X, Y, Z light position
    float fadeRadii[2]; // inner/outer fade distances
    int32_t rgb[3];     // R, G, B
} LightingVector;

class MapData : public IParsedData {
  public:
    // Cube data section (chunk type 0x01)
    int32_t mCubeMin[3]; // grid min bounds
    int32_t mCubeMax[3]; // grid max bounds
    std::vector<CubeData> mCubes;

    // Camera nodes section (chunk type 0x03)
    std::vector<CameraNode> mCameraNodes;

    // Lighting section (chunk type 0x04)
    std::vector<LightingVector> mLightingVectors;

    MapData() {
        mCubeMin[0] = mCubeMin[1] = mCubeMin[2] = 0;
        mCubeMax[0] = mCubeMax[1] = mCubeMax[2] = 0;
    }
};

class MapHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MapBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MapCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MapModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MapFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, MapCodeExporter) REGISTER(Header, MapHeaderExporter) REGISTER(Binary, MapBinaryExporter)
                     REGISTER(Modding, MapModdingExporter) };
    }

    bool HasModdedDependencies() override {
        return true;
    }
};
} // namespace BK64
