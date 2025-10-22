#include "WallTypeFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "libedl/edl.h"
#include <iomanip>

#define HEX(c) (c < 0 ? "-0x" : "0x") << std::hex << std::setfill('0') << std::abs(((int)c)) << std::dec
#define U_HEX(c) "0x" << std::hex << std::setfill('0') << ((int)c) << std::dec

ExportResult WallTypeHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern WallType " << symbol << "[];\n";

    return std::nullopt;
}

ExportResult WallTypeCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto data = std::static_pointer_cast<WallTypeData>(raw)->list;

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "WallType " << symbol << "[] = {\n";

    for (int i = 0; i < data.size(); i++) {
        int id = i + 1;
        write << tab_t << "{" << "\n";
        write << dtab_t << HEX(data[i].x) << ",\n";
        write << dtab_t << HEX(data[i].y) << ",\n";
        write << dtab_t << HEX(data[i].point2) << ",\n";
        write << dtab_t << HEX(data[i].nextwall) << ",\n";
        write << dtab_t << HEX(data[i].nextsector) << ",\n";
        write << dtab_t << HEX(data[i].cstat) << ",\n";
        write << dtab_t << HEX(data[i].picnum) << ",\n";
        write << dtab_t << HEX(data[i].overpicnum) << ",\n";
        write << dtab_t << HEX(data[i].unk14) << ",\n";
        write << dtab_t << HEX(data[i].unk16) << ",\n";
        write << dtab_t << HEX(data[i].unk18) << ",\n";
        write << dtab_t << HEX(data[i].sectnum) << ",\n";
        write << dtab_t << U_HEX(data[i].shade) << ",\n";
        write << dtab_t << U_HEX(data[i].unk1D) << ",\n";
        write << dtab_t << U_HEX(data[i].unk1E) << ",\n";
        write << dtab_t << U_HEX(data[i].unk1F) << ",\n";
        write << dtab_t << U_HEX(data[i].unk20) << ",\n";
        write << dtab_t << U_HEX(data[i].pal) << ",\n";
        write << dtab_t << U_HEX(data[i].xrepeat) << ",\n";
        write << dtab_t << U_HEX(data[i].yrepeat) << ",\n";
        write << dtab_t << U_HEX(data[i].xpanning) << ",\n";
        write << dtab_t << U_HEX(data[i].ypanning) << ",\n";
        write << dtab_t << U_HEX(data[i].pad3[0]) << ",\n";
        write << dtab_t << U_HEX(data[i].pad3[1]) << ",\n";
        write << tab_t << "}," << "\n";
    }

    write << "};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// size: 0x" << std::hex << std::uppercase << (data.size() * sizeof(WallType)) << "\n";
    }

    return std::nullopt;
}

ExportResult WallTypeBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<WallTypeData>(raw)->list;

    WriteHeader(writer, Torch::ResourceType::WallType, 0);
    writer.Write((uint32_t) data.size());

    for (const auto &item: data) {
        writer.Write(item.x);
        writer.Write(item.y);
        writer.Write(item.point2);
        writer.Write(item.nextwall);
        writer.Write(item.nextsector);
        writer.Write(item.cstat);
        writer.Write(item.picnum);
        writer.Write(item.overpicnum);
        writer.Write(item.unk14);
        writer.Write(item.unk16);
        writer.Write(item.unk18);
        writer.Write(item.sectnum);
        writer.Write(item.shade);
        writer.Write(item.unk1D);
        writer.Write(item.unk1E);
        writer.Write(item.unk1F);
        writer.Write(item.unk20);
        writer.Write(item.pal);
        writer.Write(item.xrepeat);
        writer.Write(item.yrepeat);
        writer.Write(item.xpanning);
        writer.Write(item.ypanning);
        writer.Write(item.pad3[2]);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> WallTypeFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto reader = Decompressor::AutoDecode(node, buffer).GetReader();
    auto count = GetSafeNode<uint32_t>(node, "count");
    reader.SetEndianness(Torch::Endianness::Big);
    std::vector<WallType> list;

    for(size_t i = 0; i < count; i++) {
        WallType type {};
        type.x = reader.ReadInt32();
        type.y = reader.ReadInt32();
        type.point2 = reader.ReadInt16();
        type.nextwall = reader.ReadInt16();
        type.nextsector = reader.ReadInt16();
        type.cstat = reader.ReadInt16();
        type.picnum = reader.ReadInt16();
        type.overpicnum = reader.ReadInt16();
        type.unk14 = reader.ReadInt16();
        type.unk16 = reader.ReadInt16();
        type.unk18 = reader.ReadInt16();
        type.sectnum = reader.ReadUInt16();
        type.shade = reader.ReadUByte();
        type.unk1D = reader.ReadUByte();
        type.unk1E = reader.ReadUByte();
        type.unk1F = reader.ReadUByte();
        type.unk20 = reader.ReadUByte();
        type.pal = reader.ReadUByte();
        type.xrepeat = reader.ReadUByte();
        type.yrepeat = reader.ReadUByte();
        type.xpanning = reader.ReadUByte();
        type.ypanning = reader.ReadUByte();
        type.pad3[0] = reader.ReadUByte();
        type.pad3[1] = reader.ReadUByte();
        list.push_back(type);
    }

    return std::make_shared<WallTypeData>(list);
}
