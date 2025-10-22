#include "MapInfoFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "libedl/edl.h"
#include <iomanip>
#include <memory>

#define HEX(c) (c < 0 ? "-0x" : "0x") << std::hex << std::setfill('0') << std::abs(c) << std::dec
#define U_HEX(c) "0x" << std::hex << std::setfill('0') << c << std::dec

ExportResult MapInfoHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern MapInfo " << symbol << "[];\n";

    return std::nullopt;
}

ExportResult MapInfoCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<MapInfoData>(raw)->list;

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "MapInfo " << symbol << "[] = {\n";

    for (int i = 0; i < data.size(); i++) {
        int id = i + 1;
        write << tab_t << "{" << "\n";
        write << dtab_t << "maps_map" << id << "_ROM_START, // "<< U_HEX(data[i].rom_start) << "\n";
        write << dtab_t << "maps_map" << id << "_ROM_END, // "<< U_HEX(data[i].rom_end) << "\n";
        write << dtab_t << "(s32)maps_map" << id << "_sectors_bin, // "<< U_HEX(data[i].sector_offset) << "\n";
        write << dtab_t << "(s32)maps_map" << id << "_walls_bin, // "<< U_HEX(data[i].wall_offset) << "\n";
        write << dtab_t << "(s32)maps_map" << id << "_sprites_bin, // "<< U_HEX(data[i].sprite_offset) << "\n";
        write << dtab_t << HEX(data[i].sectors) << ",\n";
        write << dtab_t << HEX(data[i].sprites) << ",\n";
        write << dtab_t << HEX(data[i].walls) << ",\n";
        write << dtab_t << HEX(data[i].xpos) << ",\n";
        write << dtab_t << HEX(data[i].ypos) << ",\n";
        write << dtab_t << HEX(data[i].zpos) << ",\n";
        write << dtab_t << HEX(data[i].ang) << ",\n";
        write << dtab_t << data[i].skytop_r << ".0f,\n";
        write << dtab_t << data[i].skytop_g << ".0f,\n";
        write << dtab_t << data[i].skytop_b << ".0f,\n";
        write << dtab_t << data[i].skybottom_r << ".0f,\n";
        write << dtab_t << data[i].skybottom_g << ".0f,\n";
        write << dtab_t << data[i].skybottom_b << ".0f,\n";
        write << tab_t << "}," << "\n";
    }
    write << "};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// size: 0x" << std::hex << std::uppercase << (data.size() * sizeof(MapInfo)) << "\n";
    }

    return offset + data.size();
}

ExportResult MapInfoBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<MapInfoData>(raw)->list;

    WriteHeader(writer, Torch::ResourceType::MapInfo, 0);
    writer.Write((uint32_t) data.size());

    for (const auto &item: data) {
        writer.Write(item.rom_start);
        writer.Write(item.rom_end);
        writer.Write(item.sector_offset);
        writer.Write(item.wall_offset);
        writer.Write(item.sprite_offset);
        writer.Write(item.sectors);
        writer.Write(item.sprites);
        writer.Write(item.walls);
        writer.Write(item.xpos);
        writer.Write(item.ypos);
        writer.Write(item.zpos);
        writer.Write(item.ang);
        writer.Write(item.skytop_r);
        writer.Write(item.skytop_g);
        writer.Write(item.skytop_b);
        writer.Write(item.skybottom_r);
        writer.Write(item.skybottom_g);
        writer.Write(item.skybottom_b);
    }

    writer.Finish(write);
    return std::nullopt;
}

MapInfo readInfo(LUS::BinaryReader& reader) {
    MapInfo map {};
    map.rom_start = reader.ReadUInt32();
    map.rom_end = reader.ReadUInt32();
    map.sector_offset = reader.ReadInt32();
    map.wall_offset = reader.ReadInt32();
    map.sprite_offset = reader.ReadInt32();
    map.sectors = reader.ReadInt32();
    map.sprites = reader.ReadInt32();
    map.walls = reader.ReadInt32();
    map.xpos = reader.ReadInt32();
    map.ypos = reader.ReadInt32();
    map.zpos = reader.ReadInt32();
    map.ang = reader.ReadInt32();
    map.skytop_r = reader.ReadFloat();
    map.skytop_g = reader.ReadFloat();
    map.skytop_b = reader.ReadFloat();
    map.skybottom_r = reader.ReadFloat();
    map.skybottom_g = reader.ReadFloat();
    map.skybottom_b = reader.ReadFloat();

    return map;
}

void readSectors(MapInfo& map, size_t id) {
    size_t count = 0;

    auto offset = map.rom_start + map.sector_offset;
    YAML::Node sector;
    sector["type"] = "DKZH:SECTOR_TYPE_TABLE";
    sector["offset"] = offset;
    sector["out"] = "map" + std::to_string(id + 1) + "/sectors.c";
    sector["symbol"] = "sectors";
    sector["count"] = map.sectors;
    Companion::Instance->AddAsset(sector);

     auto result = std::static_pointer_cast<SectorTypeData>(Companion::Instance->GetParseDataByAddr(offset).value().data.value());

     for(size_t i = 0; i < map.sectors; i++){
         count += result->list[i].floorvtxnum;
         count += result->list[i].ceilingvtxnum;
     }

    YAML::Node vtx;
    vtx["type"] = "DKZH:VERTEX";
    vtx["offset"] = map.rom_start;
    vtx["count"] = count * 3;
    vtx["out"] = "map" + std::to_string(id + 1) + "/vertex.c";
    vtx["symbol"] = "vectors";
    Companion::Instance->AddAsset(vtx);
}

void readSprites(MapInfo& map, size_t id) {
    auto offset = map.rom_start + map.sprite_offset;
    YAML::Node sprite;
    sprite["type"] = "DKZH:SPRITE_TYPE_TABLE";
    sprite["offset"] = offset;
    sprite["out"] = "map" + std::to_string(id + 1) + "/sprites.c";
    sprite["symbol"] = "sprites";
    sprite["count"] = map.sprites;
    Companion::Instance->AddAsset(sprite);
}

void readWalls(MapInfo& map, size_t id) {
    auto offset = map.rom_start + map.wall_offset;
    YAML::Node sprite;
    sprite["type"] = "DKZH:WALL_TYPE_TABLE";
    sprite["offset"] = offset;
    sprite["out"] = "map" + std::to_string(id + 1) + "/walls.c";
    sprite["symbol"] = "walls";
    sprite["count"] = map.walls;
    Companion::Instance->AddAsset(sprite);
}

std::optional<std::shared_ptr<IParsedData>> MapInfoFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto reader = Decompressor::AutoDecode(node, buffer).GetReader();
    auto count = GetSafeNode<uint32_t>(node, "count");
    reader.SetEndianness(Torch::Endianness::Big);
    std::vector<MapInfo> list;

    for(size_t i = 0; i < count; i++){
        MapInfo map = readInfo(reader);
        readSectors(map, i);
        readSprites(map, i);
        readWalls(map, i);
        list.push_back(map);
    }

    return std::make_shared<MapInfoData>(list);
}
