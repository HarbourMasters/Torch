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
 * Prop: a sprite, model or actor slot - 12 bytes, kept in the ROM's big-endian order
 * Decomp: sprite_prop_s, model_prop_s and actor_prop_s in prop.h
 *
 * The halfword at 0x0A picks the layout: bit0 set is an actor slot, else bit1 set
 * is a model, else a sprite. Every layout has position[3] (s16[3]) at 0x04.
 *
 *   Sprite:     0x00 spriteId:12, unk0_19:1, rgb_remove_red/green/blue:3 each,
 *                    scale:8 (percent), isMirrored:1, pad0_0:1 (u32)
 *               0x0A frame:5, unk8_10:5 (phase), flags:4, isModelProp:1, isActorProp:1
 *   Model:      0x00 modelId:12, pad0_19:4 (u16); 0x02 yaw, 0x03 roll (degrees / 2)
 *               0x0A scale (percent); 0x0B flags:6, isModelProp:1, isActorProp:1
 *   Actor slot: 0x00 marker (a runtime pointer, 0 in the ROM)
 *               0x0A frame:5, unk8_10:5, flags:4, isModelProp:1, isActorProp:1
 *
 * Asset ID = spriteId + 0x572, modelId + 0x2D1.
 */
typedef struct Prop {
    uint8_t raw[12];
} Prop;

/**
 * CubeData: one cell of the 32×32×32 spatial partition grid
 *
 * Cells come in file order: x outermost, z innermost.
 */
typedef struct CubeData {
    int32_t x;
    int32_t y;
    int32_t z;
    bool listedEmpty = false; // holds nothing, but the file still writes its node and prop blocks
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

// The setup as the ROM stores it once decompressed, which is also the decomp's .lvl_setup.bin.
std::shared_ptr<MapData> ReadMapRom(const std::vector<uint8_t>& bytes, const std::string& symbol);
std::vector<uint8_t> WriteMapRom(const MapData& map);
// A BKMap resource, as MapBinaryExporter writes it.
std::shared_ptr<MapData> ReadMapResource(const std::vector<uint8_t>& resource);
std::string MapToYaml(const MapData& map, const std::string& symbol);
bool MapFromYaml(const std::string& text, MapData& out, std::string& error);

class MapFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, MapCodeExporter) REGISTER(Header, MapHeaderExporter) REGISTER(Binary, MapBinaryExporter)
                     REGISTER(Modding, MapModdingExporter) };
    }

    bool HasModdedDependencies() override {
        return true;
    }
    bool SupportModdedAssets() override {
        return true;
    }
};
} // namespace BK64
