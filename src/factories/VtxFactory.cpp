#include "VtxFactory.h"
#include "utils/MIODecoder.h"
#include <iomanip>

void VtxCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto vtx = std::static_pointer_cast<VtxData>(raw);
    auto symbol = node["symbol"].as<std::string>();

    write << "Vtx " << symbol << "[] = {\n" << tab;
    for (int i = 0; i < vtx->mVtxs.size(); ++i) {
        auto v = vtx->mVtxs[i];
        write << "{{{" << v.ob[0] << ", " << v.ob[1] << ", " << v.ob[2] << "}, " << v.flag << ", {" << v.tc[0] << ", " << v.tc[1] << "}, {" << v.cn[0] << ", " << v.cn[1] << ", " << v.cn[2] << ", " << v.cn[3] << "}}},\n";
        if(i <= vtx->mVtxs.size() - 2)
            write << tab;
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
    auto offset = node["offset"].as<size_t>();
    auto size = node["size"].as<size_t>();
    auto symbol = node["symbol"].as<std::string>();

    auto decoded = MIO0Decoder::Decode(buffer, mio0);
    LUS::BinaryReader reader(decoded.data() + offset, size);
    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<VtxRaw> vtxs;

    for(size_t i = 0; i < size; i += 0x10) {
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

        vtxs.push_back(VtxRaw({
           {x, y, z}, flag, {tc1, tc2}, {cn1, cn2, cn3, cn4}
       }));
    }

    return std::make_shared<VtxData>(vtxs);
}