#include "CourseVtx.h"

#include "Companion.h"
#include "utils/Decompressor.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define COL(c) "0x" << std::hex << std::setw(2) << std::setfill('0') << c

ExportResult MK64::CourseVtxHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Vtx " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult MK64::CourseVtxCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto vtx = std::static_pointer_cast<CourseVtxData>(raw)->mVtxs;
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");

    write << "Vtx " << symbol << "[] = {\n";

    for (int i = 0; i < vtx.size(); ++i) {
        auto v = vtx[i];

        auto x = v.ob[0];
        auto y = v.ob[1];
        auto z = v.ob[2];

        auto f = v.flag;

        auto tc1 = v.tc[0];
        auto tc2 = v.tc[1];

        auto c1 = (uint16_t) v.cn[0];
        auto c2 = (uint16_t) v.cn[1];
        auto c3 = (uint16_t) v.cn[2];
        auto c4 = (uint16_t) v.cn[3];

        if(i <= vtx.size() - 1) {
            write << fourSpaceTab;
        }

        // {{{ x, y, z }, { tc1, tc2 }, { c1, c2, c3, c4 }}}
        write << "{{{" << NUM(x) << ", " << NUM(y) << ", " << NUM(z) << "}, " << NUM(flag) << " {" << NUM(tc1) << ", " << NUM(tc2) << "}, {" << COL(c1) << ", " << COL(c2) << ", " << COL(c3) << ", " << COL(c4) << "}}},\n";
    }
    write << "};\n";

    return offset + vtx.size() * sizeof(Vtx);
}

ExportResult MK64::CourseVtxBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto vtx = std::static_pointer_cast<CourseVtxData>(raw);
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::Vtx, 0);
    writer.Write((uint32_t) vtx->mVtxs.size());
    for(auto v : vtx->mVtxs) {
        writer.Write(v.ob[0]);
        writer.Write(v.ob[1]);
        writer.Write(v.ob[2]);
        writer.Write(v.flag);
        writer.Write(v.tc[0]);
        writer.Write(v.tc[1]);
        writer.Write(v.cn[0]);
        writer.Write(v.cn[1]);
        writer.Write(v.cn[2]);
        writer.Write(v.cn[3]);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> MK64::CourseVtxFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto count = GetSafeNode<size_t>(node, "count");

    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, count * sizeof(CourseVtx));

    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<Vtx> vertices;

    for(size_t i = 0; i < count; i++) {
        const auto x = reader.ReadInt16();
        const auto y = reader.ReadInt16();
        const auto z = reader.ReadInt16();
        const auto tc1 = reader.ReadInt16();
        const auto tc2 = reader.ReadInt16();
        auto cn1 = reader.ReadUByte();
        auto cn2 = reader.ReadUByte();
        auto cn3 = reader.ReadUByte();
        auto cn4 = reader.ReadUByte();

        uint16_t flag = cn1 & 3;
        flag |= (cn2 << 2) & 0xC;

        cn1 &= 0xFC;
        cn2 &= 0xFC;
    
        vertices->v.flag = flags;
        cn4 = 0xFF;

        vertices.push_back(Vtx({
           {x, y, z}, flag, {tc1, tc2}, {cn1, cn2, cn3, cn4}
       }));
    }

    return std::make_shared<CourseVtxData>(vertices);
}