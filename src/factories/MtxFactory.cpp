#include "MtxFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define COL(c) std::dec << std::setfill(' ') << std::setw(3) << c

void MtxHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern Mtx " << symbol << "[];\n";
}

void MtxCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto m = std::static_pointer_cast<MtxData>(raw)->mMtxs;
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    if (IS_SEGMENTED(offset)) {
        offset = SEGMENT_OFFSET(offset);
    }

    if (Companion::Instance->IsDebug()) {
        if (IS_SEGMENTED(offset)) {
            offset = SEGMENT_OFFSET(offset);
        }
        write << "// 0x" << std::hex << std::uppercase << offset << "\n";
    }

    write << "Mtx " << symbol << "[] = {\n";

    for (int i = 0; i < m.size(); ++i) {

        auto m1 = m[i].mtx[0][0];
        auto m2 = m[i].mtx[0][1];
        auto m3 = m[i].mtx[0][2];
        auto m4 = m[i].mtx[0][3];

        auto m5 = m[i].mtx[1][0];
        auto m6 = m[i].mtx[1][1];
        auto m7 = m[i].mtx[1][2];
        auto m8 = m[i].mtx[1][3];

        auto m9 = m[i].mtx[2][0];
        auto m10 = m[i].mtx[2][1];
        auto m11 = m[i].mtx[2][2];
        auto m12 = m[i].mtx[2][3];

        auto m13 = m[i].mtx[3][0];
        auto m14 = m[i].mtx[3][1];
        auto m15 = m[i].mtx[3][2];
        auto m16 = m[i].mtx[3][3];

        // {{ m1, m2, m3,     m4 },
        //  { m5, m6, m7,     m8 },
        //  { m9, m10, m11,  m12 },
        //  { m13, m14, m15, m16 }},
        write << fourSpaceTab << "{{" << NUM(m1) << ", " << NUM(m2) << ", " << NUM(m3) << ", " << NUM(m4) << "},\n";
        write << fourSpaceTab << " {" << NUM(m5) << ", " << NUM(m6) << ", " << NUM(m7) << ", " << NUM(m8) << "},\n";
        write << fourSpaceTab << " {" << NUM(m9) << ", " << NUM(m10) << ", " << NUM(m11) << ", " << NUM(m12) << "},\n";
        write << fourSpaceTab << " {" << NUM(m13) << ", " << NUM(m14) << ", " << NUM(m15) << ", " << NUM(m16) << "}},\n";
    }

    write << "};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// count: " << std::to_string(m.size()) << " Mtxs\n";
        write << "// 0x" << std::hex << std::uppercase << (offset + (sizeof(MtxRaw) * m.size())) << "\n";
    }

    write << "\n";
}

void MtxBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto mtx = std::static_pointer_cast<MtxData>(raw);
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::Vertex, 0);
    writer.Write((uint32_t) mtx->mMtxs.size());
    for(auto m : mtx->mMtxs) {
        writer.Write(m.mtx[0][0]);
        writer.Write(m.mtx[0][1]);
        writer.Write(m.mtx[0][2]);
        writer.Write(m.mtx[0][3]);
        writer.Write(m.mtx[1][0]);
        writer.Write(m.mtx[1][1]);
        writer.Write(m.mtx[1][2]);
        writer.Write(m.mtx[1][3]);
        writer.Write(m.mtx[2][0]);
        writer.Write(m.mtx[2][1]);
        writer.Write(m.mtx[2][2]);
        writer.Write(m.mtx[2][3]);
        writer.Write(m.mtx[3][0]);
        writer.Write(m.mtx[3][1]);
        writer.Write(m.mtx[3][2]);
        writer.Write(m.mtx[3][3]);
    }

    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> MtxFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto count = GetSafeNode<size_t>(node, "count");

    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, count * sizeof(MtxRaw));

    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<MtxRaw> matrix;

    for(size_t i = 0; i < count; i++) {
        auto m1 = reader.ReadInt32();
        auto m2 = reader.ReadInt32();
        auto m3 = reader.ReadInt32();
        auto m4 = reader.ReadInt32();
        auto m5 = reader.ReadInt32();
        auto m6 = reader.ReadInt32();
        auto m7 = reader.ReadInt32();
        auto m8 = reader.ReadInt32();
        auto m9 = reader.ReadInt32();
        auto m10 = reader.ReadInt32();
        auto m11 = reader.ReadInt32();
        auto m12 = reader.ReadInt32();
        auto m13 = reader.ReadInt32();
        auto m14 = reader.ReadInt32();
        auto m15 = reader.ReadInt32();
        auto m16 = reader.ReadInt32();

        matrix.push_back(MtxRaw({
           {{m1, m2, m3, m4},
           {m5, m6, m7, m8},
           {m9, m10, m11, m12},
           {m13, m14, m15, m16}},
       }));
    }

    return std::make_shared<MtxData>(matrix);
}
