#include "SectorTypeFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "libedl/edl.h"
#include <iomanip>

#define HEX(c) (c < 0 ? "-0x" : "0x") << std::hex << std::setfill('0') << std::abs(((int)c)) << std::dec
#define U_HEX(c) "0x" << std::hex << std::setfill('0') << ((int)c) << std::dec

ExportResult SectorTypeHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern SectorType " << symbol << "[];\n";

    return std::nullopt;
}

ExportResult SectorTypeCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto data = std::static_pointer_cast<SectorTypeData>(raw)->list;

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "SectorType " << symbol << "[] = {\n";

    for (int i = 0; i < data.size(); i++) {
        int id = i + 1;
        write << tab_t << "{" << "\n";
        write << dtab_t << HEX(data[i].ceilingz) << ",\n";
        write << dtab_t << HEX(data[i].floorz) << ",\n";
        write << dtab_t << HEX(data[i].wallptr) << ",\n";
        write << dtab_t << HEX(data[i].wallnum) << ",\n";
        write << dtab_t << HEX(data[i].ceilingstat) << ",\n";
        write << dtab_t << HEX(data[i].floorstat) << ",\n";
        write << dtab_t << HEX(data[i].ceilingpicnum) << ",\n";
        write << dtab_t << HEX(data[i].ceilingheinum) << ",\n";
        write << dtab_t << HEX(data[i].floorpicnum) << ",\n";
        write << dtab_t << HEX(data[i].floorheinum) << ",\n";
        write << dtab_t << HEX(data[i].unk18) << ",\n";
        write << dtab_t << HEX(data[i].unk1A) << ",\n";
        write << dtab_t << HEX(data[i].unk1C) << ",\n";
        write << dtab_t << U_HEX(data[i].floorvtxptr) << ",\n";
        write << dtab_t << U_HEX(data[i].ceilingvtxptr) << ",\n";
        write << dtab_t << U_HEX(data[i].ceilingshade) << ",\n";
        write << dtab_t << U_HEX(data[i].ceilingpal) << ",\n";
        write << dtab_t << "{ 0, 0 }" << ",\n";
        write << dtab_t << U_HEX(data[i].floorshade) << ",\n";
        write << dtab_t << U_HEX(data[i].floorpal) << ",\n";
        write << dtab_t << "{ 0, 0 }" << ",\n";
        write << dtab_t << U_HEX(data[i].unk2A) << ",\n";
        write << dtab_t << U_HEX(data[i].floorvtxnum) << ",\n";
        write << dtab_t << U_HEX(data[i].ceilingvtxnum) << ",\n";
        write << dtab_t << "{ 0, 0, 0 }" << ",\n";
        write << tab_t << "}," << "\n";
    }

    write << "};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// size: 0x" << std::hex << std::uppercase << (data.size() * sizeof(SectorType)) << "\n";
    }

    return std::nullopt;
}

ExportResult SectorTypeBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<SectorTypeData>(raw)->list;

    WriteHeader(writer, Torch::ResourceType::SectorType, 0);
    writer.Write((uint32_t) data.size());

    for (const auto &item: data) {
        writer.Write(item.ceilingz);
        writer.Write(item.floorz);
        writer.Write(item.wallptr);
        writer.Write(item.wallnum);
        writer.Write(item.ceilingstat);
        writer.Write(item.floorstat);
        writer.Write(item.ceilingpicnum);
        writer.Write(item.ceilingheinum);
        writer.Write(item.floorpicnum);
        writer.Write(item.floorheinum);
        writer.Write(item.unk18);
        writer.Write(item.unk1A);
        writer.Write(item.unk1C);
        writer.Write(item.floorvtxptr);
        writer.Write(item.ceilingvtxptr);
        writer.Write(item.ceilingshade);
        writer.Write(item.ceilingpal);
        writer.Write(item.pad[2]);
        writer.Write(item.floorshade);
        writer.Write(item.floorpal);
        writer.Write(item.pad2[2]);
        writer.Write(item.unk2A);
        writer.Write(item.floorvtxnum);
        writer.Write(item.ceilingvtxnum);
        writer.Write(item.pad3[3]);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SectorTypeFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto reader = Decompressor::AutoDecode(node, buffer).GetReader();
    auto count = GetSafeNode<uint32_t>(node, "count");
    reader.SetEndianness(Torch::Endianness::Big);
    std::vector<SectorType> list;

    for(size_t i = 0; i < count; i++) {
        SectorType type {};
        type.ceilingz = reader.ReadInt32();
        type.floorz = reader.ReadInt32();
        type.wallptr = reader.ReadInt16();
        type.wallnum = reader.ReadInt16();
        type.ceilingstat = reader.ReadInt16();
        type.floorstat = reader.ReadInt16();
        type.ceilingpicnum = reader.ReadInt16();
        type.ceilingheinum = reader.ReadInt16();
        type.floorpicnum = reader.ReadInt16();
        type.floorheinum = reader.ReadInt16();
        type.unk18 = reader.ReadInt16();
        type.unk1A = reader.ReadInt16();
        type.unk1C = reader.ReadInt16();
        type.floorvtxptr = reader.ReadUInt16();
        type.ceilingvtxptr = reader.ReadUInt16();
        type.ceilingshade = reader.ReadUInt16();
        type.ceilingpal = reader.ReadUInt16();
        type.pad[0] = reader.ReadUByte();
        type.pad[1] = reader.ReadUByte();
        type.floorshade = reader.ReadUByte();
        type.floorpal = reader.ReadUByte();
        type.pad2[0] = reader.ReadUByte();
        type.pad2[1] = reader.ReadUByte();
        type.unk2A = reader.ReadUByte();
        type.floorvtxnum = reader.ReadUByte();
        type.ceilingvtxnum = reader.ReadUByte();
        type.pad3[0] = reader.ReadUByte();
        type.pad3[1] = reader.ReadUByte();
        type.pad3[2] = reader.ReadUByte();

        list.push_back(type);
    }

    return std::make_shared<SectorTypeData>(list);
}
