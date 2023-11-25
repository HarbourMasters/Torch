#include "CourseVtx.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/MIODecoder.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define COL(c) "0x" << std::hex << std::setw(2) << std::setfill('0') << c

void MK64::VtxHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = node["symbol"] ? node["symbol"].as<std::string>() : entryName;

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern CourseVtx " << symbol << "[];\n";
}

void MK64::VtxCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto vtx = std::static_pointer_cast<VtxData>(raw)->mVtxs;

    if (!node["symbol"]) {
        SPDLOG_ERROR("Asset in yaml missing entry for symbol.\nEx. symbol: myVariableNameHere");
        return;
    }

    const auto symbol = node["symbol"].as<std::string>();
    const auto offset = node["offset"].as<uint32_t>();

    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::hex << std::uppercase << offset << "\n";
    }

    write << "CourseVtx " << symbol << "[] = {\n";

    for (int i = 0; i < vtx.size(); ++i) {
        auto v = vtx[i];

        auto x = v.ob[0];
        auto y = v.ob[1];
        auto z = v.ob[2];

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
        write << "{{{" << NUM(x) << ", " << NUM(y) << ", " << NUM(z) << "}, {" << NUM(tc1) << ", " << NUM(tc2) << "}, {" << COL(c1) << ", " << COL(c2) << ", " << COL(c3) << ", " << COL(c4) << "}}},\n";
    }

    if (Companion::Instance->IsDebug()) {
        write << fourSpaceTab << "// 0x" << std::hex << std::uppercase << (offset + (16 * vtx.size())) << "\n";
    }

    write << "};\n\n";
}

void MK64::VtxBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto vtx = std::static_pointer_cast<VtxData>(raw);
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::Vertex, 0);
    writer.Write((uint32_t) vtx->mVtxs.size());
    for(auto v : vtx->mVtxs) {
        writer.Write(v.ob[0]);
        writer.Write(v.ob[1]);
        writer.Write(v.ob[2]);
        writer.Write(v.tc[0]);
        writer.Write(v.tc[1]);
        writer.Write(v.cn[0]);
        writer.Write(v.cn[1]);
        writer.Write(v.cn[2]);
        writer.Write(v.cn[3]);
    }

    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> MK64::VtxFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    
    if (!node["mio0"]) {
        SPDLOG_ERROR("yaml missing entry for mio0.\nEx. mio0: 0x10100");
        return std::nullopt;
    }

    if (!node["offset"]) {
        SPDLOG_ERROR("yaml missing entry for offset.\nEx. offset: 0x100");
        return std::nullopt;
    }

    if (!node["count"]) {
        SPDLOG_ERROR("Asset in yaml missing entry for vtx count.\nEx. count: 30\nThis means 30 vertices (an array size of 30).");
        return std::nullopt;
    }
    
    auto mio0 = node["mio0"].as<size_t>();
    auto offset = node["offset"].as<uint32_t>();
    auto count = node["count"].as<size_t>();

    auto decoded = MIO0Decoder::Decode(buffer, mio0);
    LUS::BinaryReader reader(decoded.data() + offset, count * sizeof(CourseVtx) );

    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<CourseVtx> vertices;

    for(size_t i = 0; i < count; i++) {
        auto x = reader.ReadInt16();
        auto y = reader.ReadInt16();
        auto z = reader.ReadInt16();
        auto tc1 = reader.ReadInt16();
        auto tc2 = reader.ReadInt16();
        auto cn1 = reader.ReadUByte();
        auto cn2 = reader.ReadUByte();
        auto cn3 = reader.ReadUByte();
        auto cn4 = reader.ReadUByte();

        vertices.push_back(CourseVtx({
           {x, y, z}, {tc1, tc2}, {cn1, cn2, cn3, cn4}
       }));
    }

    return std::make_shared<VtxData>(vertices);
}