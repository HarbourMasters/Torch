#include "SpriteTypeFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "libedl/edl.h"
#include <iomanip>

#define HEX(c) (c < 0 ? "-0x" : "0x") << std::hex << std::setfill('0') << std::abs(((int)c)) << std::dec
#define U_HEX(c) "0x" << std::hex << std::setfill('0') << ((int)c) << std::dec

ExportResult SpriteTypeHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern SpriteType " << symbol << "[];\n";

    return std::nullopt;
}

ExportResult SpriteTypeCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto data = std::static_pointer_cast<SpriteTypeData>(raw)->list;

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "SpriteType " << symbol << "[] = {\n";

    for (int i = 0; i < data.size(); i++) {
        int id = i + 1;
        write << tab_t << "{" << "\n";
        write << dtab_t << HEX(data[i].x) << ",\n";
        write << dtab_t << HEX(data[i].y) << ",\n";
        write << dtab_t << HEX(data[i].z) << ",\n";
        write << dtab_t << HEX(data[i].cstat) << ",\n";
        write << dtab_t << HEX(data[i].picnum) << ",\n";
        write << dtab_t << HEX(data[i].sectnum) << ",\n";
        write << dtab_t << HEX(data[i].statnum) << ",\n";
        write << dtab_t << HEX(data[i].ang) << ",\n";
        write << dtab_t << HEX(data[i].unk16) << ",\n";
        write << dtab_t << HEX(data[i].unk18) << ",\n";
        write << dtab_t << HEX(data[i].unk1A) << ",\n";
        write << dtab_t << HEX(data[i].unk1C) << ",\n";
        write << dtab_t << HEX(data[i].lotag) << ",\n";
        write << dtab_t << HEX(data[i].hitag) << ",\n";
        write << dtab_t << HEX(data[i].unk22) << ",\n";
        write << dtab_t << U_HEX(data[i].unk24) << ",\n";
        write << dtab_t << U_HEX(data[i].unk25) << ",\n";
        write << dtab_t << U_HEX(data[i].clipdist) << ",\n";
        write << dtab_t << U_HEX(data[i].xrepeat) << ",\n";
        write << dtab_t << U_HEX(data[i].yrepeat) << ",\n";
        write << dtab_t << U_HEX(data[i].unk29) << ",\n";
        write << dtab_t << U_HEX(data[i].unk2A) << ",\n";
        write << dtab_t << U_HEX(data[i].unk2B) << ",\n";
        write << tab_t << "}," << "\n";
    }

    write << "};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// size: 0x" << std::hex << std::uppercase << (data.size() * sizeof(SpriteType)) << "\n";
    }

    return std::nullopt;
}

ExportResult SpriteTypeBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<SpriteTypeData>(raw)->list;

    WriteHeader(writer, Torch::ResourceType::SpriteType, 0);
    writer.Write((uint32_t) data.size());

    for (const auto &item: data) {
        writer.Write(item.x);
        writer.Write(item.y);
        writer.Write(item.z);
        writer.Write(item.cstat);
        writer.Write(item.picnum);
        writer.Write(item.sectnum);
        writer.Write(item.statnum);
        writer.Write(item.ang);
        writer.Write(item.unk16);
        writer.Write(item.unk18);
        writer.Write(item.unk1A);
        writer.Write(item.unk1C);
        writer.Write(item.lotag);
        writer.Write(item.hitag);
        writer.Write(item.unk22);
        writer.Write(item.unk24);
        writer.Write(item.unk25);
        writer.Write(item.clipdist);
        writer.Write(item.xrepeat);
        writer.Write(item.yrepeat);
        writer.Write(item.unk29);
        writer.Write(item.unk2A);
        writer.Write(item.unk2B);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SpriteTypeFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto reader = Decompressor::AutoDecode(node, buffer).GetReader();
    auto count = GetSafeNode<uint32_t>(node, "count");
    reader.SetEndianness(Torch::Endianness::Big);
    std::vector<SpriteType> list;

    for(size_t i = 0; i < count; i++) {
        SpriteType type {};
        type.x = reader.ReadInt32();
        type.y = reader.ReadInt32();
        type.z = reader.ReadInt32();
        type.cstat = reader.ReadInt16();
        type.picnum = reader.ReadInt16();
        type.sectnum = reader.ReadInt16();
        type.statnum = reader.ReadInt16();
        type.ang = reader.ReadInt16();
        type.unk16 = reader.ReadInt16();
        type.unk18 = reader.ReadInt16();
        type.unk1A = reader.ReadInt16();
        type.unk1C = reader.ReadInt16();
        type.lotag = reader.ReadInt16();
        type.hitag = reader.ReadInt16();
        type.unk22 = reader.ReadInt16();
        type.unk24 = reader.ReadUByte();
        type.unk25 = reader.ReadUByte();
        type.clipdist = reader.ReadUByte();
        type.xrepeat = reader.ReadUByte();
        type.yrepeat = reader.ReadUByte();
        type.unk29 = reader.ReadUByte();
        type.unk2A = reader.ReadUByte();
        type.unk2B = reader.ReadUByte();

        list.push_back(type);
    }

    return std::make_shared<SpriteTypeData>(list);
}
