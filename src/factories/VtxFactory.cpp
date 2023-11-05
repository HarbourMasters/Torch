#include "VtxFactory.h"
#include "utils/MIODecoder.h"
#include <iomanip>

void VtxCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto vtx = std::static_pointer_cast<VtxData>(raw)->mVtxs;
    auto symbol = node["symbol"].as<std::string>();

    write << "Vtx " << symbol << "[] = {\n";

    for (int i = 0; i < vtx.size(); ++i) {
        auto v = vtx[i];

        auto x = v.ob[0];
        auto y = v.ob[1];
        auto z = v.ob[2];

        auto flag = v.flag;

        auto tc1 = v.tc[0];
        auto tc2 = v.tc[1];

        auto c1 = v.cn[0];
        auto c2 = v.cn[1];
        auto c3 = v.cn[2];
        auto c4 = v.cn[3];

        write << fourSpaceTab;

        // {{{ x, y, z }, f, { tc1, tc2 }, { c1, c2, c3, c4 }}}
        write << "{{{" << x << ", " << y << ", " << z << "}, " << flag << ", {" << tc1 << ", " << tc2 << "}, {" << c1 << ", " << c2 << ", " << c3 << ", " << c4 << "}}},\n";

    }
    write << "};\n";
}

void VtxBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto vtx = std::static_pointer_cast<VtxData>(raw);
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::Vertex, 0);
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
}

std::optional<std::shared_ptr<IParsedData>> VtxFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto mio0 = node["mio0"].as<size_t>();
    auto offset = node["offset"].as<uint32_t>();
    auto count = node["count"].as<size_t>();
    auto symbol = node["symbol"].as<std::string>();

    auto decoded = MIO0Decoder::Decode(buffer, mio0);
    LUS::BinaryReader reader(decoded.data() + offset, (count * sizeof(VtxRaw)) );

    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<VtxRaw> vertices;

    for(size_t i = 0; i < count; i++) {
        auto x = reader.ReadInt16();
        auto y = reader.ReadInt16();
        auto z = reader.ReadInt16();
        auto flag = reader.ReadUInt16();
        auto tc1 = reader.ReadInt16();
        auto tc2 = reader.ReadInt16();
        auto cn1 = reader.ReadUByte();
        auto cn2 = reader.ReadUByte();
        auto cn3 = reader.ReadUByte();
        auto cn4 = reader.ReadUByte();

        vertices.push_back(VtxRaw({
           {x, y, z}, flag, {tc1, tc2}, {cn1, cn2, cn3, cn4}
       }));
    }

    return std::make_shared<VtxData>(vertices);
}