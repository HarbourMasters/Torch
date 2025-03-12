#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

typedef struct {
    uint8_t pad0[0x4];             // 4 bytes of padding
    int16_t geo_list_offset_4;     // geometry layout offset
    int16_t texture_list_offset_8; // texture setup offset (usually 00 38)
    int16_t geo_typ_A; // Geo Types: 0000=normal, 0002= Trilinear MipMapping (RGBA16), 0004=Env mapping, 0006=Trilinear
                       // MipMapping (RGBA16) + Environment Mapping
    int32_t gfx_list_offset_C;            // Disply List Setup Offset
    int32_t vtx_list_offset_10;           // Vertex Store setup Offset
    int32_t unk14;                        // unknown(but related to hitboxes)
    int32_t animation_list_offset_18;     // Animation Setup
    int32_t collision_list_offset_1C;     // Collision Setup
    int32_t unk20;                        // Effect Setup end address
    int32_t effects_list_setup_24;        // Effect Setup
    int32_t unk28;                        // unknown [00 00 00 00]
    int32_t animated_texture_list_offset; // AnimTexture offset[4]
    int32_t tri_count;                    // tri count
    int32_t vert_count;                   // vert_count
    int32_t unk38;                        // unknown [00 00 00 00]
} BKModelBin;

class ModelData : public IParsedData {
  public:
    BKModelBin mData;

    ModelData() = default;
    
    /* ModelData(uint8_t pad0, int16_t geo_list_offset_4, int16_t texture_list_offset_8, int16_t geo_typ_A, int32_t gfx_list_offset_C, 
               int32_t vtx_list_offset_10, int32_t unk14, int32_t animation_list_offset_18, int32_t collision_list_offset_1C, 
               int32_t unk20, int32_t effects_list_setup_24, int32_t unk28, int32_t animated_texture_list_offset, int32_t tri_count, 
               int32_t vert_count, int32_t unk38) 
        : //mData(), geo_list_offset_4(geo_list_offset_4), texture_list_offset_8(texture_list_offset_8), geo_typ_A(geo_typ_A), 
          gfx_list_offset_C(gfx_list_offset_C), vtx_list_offset_10(vtx_list_offset_10), unk14(unk14), animation_list_offset_18(animation_list_offset_18), 
          collision_list_offset_1C(collision_list_offset_1C), unk20(unk20), effects_list_setup_24(effects_list_setup_24), unk28(unk28), 
          animated_texture_list_offset(animated_texture_list_offset), tri_count(tri_count), vert_count(vert_count), unk38(unk38) {
    } */
   ModelData(BKModelBin mData) : mData(mData) {}
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
};
} // namespace BK64
