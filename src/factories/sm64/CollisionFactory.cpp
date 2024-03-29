#include "CollisionFactory.h"

#include <Companion.h>
#include <utils/Decompressor.h>

#define BASE(val, cmd, enumname) val == Companion::Instance->GetValueFromEnum(enumname, cmd)

#define COL_INIT(v) BASE(v, "TERRAIN_LOAD_VERTICES", "TerrainLoadCommands")
#define COL_TRI_STOP(v) BASE(v, "TERRAIN_LOAD_END", "TerrainLoadCommands")
#define COL_TRI_INIT(surf) BASE(v, surf, "SurfaceTypes")

#define SURFACE_TYPE(type) Companion::Instance->GetEnumFromValue("SurfaceTypes", type).value_or(std::to_string(type))

enum class ProcessingState {
    None,
    Vertex,
    Triangle,
    Special
};

ExportResult SM64::CollisionCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto collision = std::static_pointer_cast<CollisionData>(raw);

    size_t cmd_size = collision->mCommandList.size();
    ProcessingState state = ProcessingState::None;

    size_t vtxCount = 0;

    for(size_t i = 0; i < collision->mCommandList.size(); i++){
        auto cmd = collision->mCommandList[i];

        if(COL_INIT(cmd)){
            vtxCount = collision->mCommandList[++i];
            write << "COL_INIT(),\n";
            write << "COL_VERTEX_INIT(" << vtxCount << "),\n";
            state = ProcessingState::Vertex;
            continue;
        }

        switch(state) {
            case ProcessingState::Vertex:
                if(vtxCount > 0){
                    auto x = collision->mCommandList[i + 0];
                    auto y = collision->mCommandList[i + 1];
                    auto z = collision->mCommandList[i + 2];

                    write << "COL_VERTEX(" << x << ", " << y << ", " << z << "),\n";
                    vtxCount--;
                } else {
                    int32_t surf = collision->mCommandList[i];
                    auto triCount = collision->mCommandList[++i];

                    write << "COL_TRI_INIT(" << SURFACE_TYPE(surf) << ", " << triCount << "),\n";
                    state = ProcessingState::Triangle;
                }
                break;
            case ProcessingState::Triangle:
                if(!COL_TRI_STOP(cmd)){
                    auto v1 = collision->mCommandList[i + 0];
                    auto v2 = collision->mCommandList[i + 1];
                    auto v3 = collision->mCommandList[i + 2];

                    write << "COL_TRI(" << v1 << ", " << v2 << ", " << v3 << "),\n";
                } else {
                    write << "COL_TRI_STOP(),\n";
                    state = ProcessingState::None;
                }
                break;
        }

        if(state == ProcessingState::Vertex){

            continue;
        }


    }

    return offset + cmd_size * sizeof(int16_t);
}


ExportResult SM64::CollisionHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto collision = std::static_pointer_cast<CollisionData>(raw);

    return std::nullopt;
}

ExportResult SM64::CollisionBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto collision = std::static_pointer_cast<CollisionData>(raw);

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SM64::CollisionFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [root, segment] = Decompressor::AutoDecode(node, buffer);

    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<int16_t> commands;
    bool processing = true;

    while(processing){
        auto cmd = reader.ReadInt16();

        if(COL_TRI_STOP(cmd)){
            processing = false;
        }

        commands.push_back(cmd);
    }

    return std::nullopt;
}