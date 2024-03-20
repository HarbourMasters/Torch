#include "ColPolyFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include <regex>


#define NUM(x, w) std::dec << std::setfill(' ') << std::setw(w) << x

SF64::ColPolyData::ColPolyData(std::vector<SF64::CollisionPoly> polys, std::vector<Vec3s> mesh): mPolys(polys), mMesh(mesh) {

}

ExportResult SF64::ColPolyHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern CollisionPoly " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult SF64::ColPolyCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto colpolys = std::static_pointer_cast<SF64::ColPolyData>(raw);
    auto off = offset;
    int i;
    if(IS_SEGMENTED(off)) {
        off = SEGMENT_OFFSET(off);
    }
    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::uppercase << std::hex << off << "\n";
    }

    write << "CollisionPoly " << symbol << "[] = {";
    int width = std::log10(colpolys->mMesh.size()) + 1;
    // i = 0;
    for(SF64::CollisionPoly poly : colpolys->mPolys) {
        // if((i++ % 2) == 0) {
            write << "\n" << fourSpaceTab;
        // }
        write << "{ " << NUM(poly.tri, width) << ",  ";
        if (poly.unk_06 == 0) {
            write << "0,  ";
        } else {
            SPDLOG_INFO("SF64:COLPOLY alert: Nonzero value of unk_06");
            write << "/* ALERT: NONZERO */ " << poly.unk_06 << ",  ";
        }
        write << NUM(poly.norm, 6) << ",  ";
        if(poly.unk_0E != 0) {
            SPDLOG_ERROR("SF64:COLPOLY error: Nonzero value found in padding");
            write << "/* ALERT: NONZERO PAD */ ";
        }
        write << NUM(poly.dist, 6) << "}, ";
    }

    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// CollisionPoly count: " << colpolys->mPolys.size() << "\n";
        write << "// 0x" << std::uppercase << std::hex << off + colpolys->mPolys.size() * sizeof(SF64::CollisionPoly) << "\n";
    }

    write << "\n";

    std::ostringstream defaultMeshSymbol;
    auto defaultMeshOffset = offset + colpolys->mPolys.size() * sizeof(SF64::CollisionPoly);
    const auto meshOffset = GetSafeNode(node, "mesh_offset", defaultMeshOffset);
    if (meshOffset != defaultMeshOffset) {
        write << "// SF64:COLPOLY alert: Gap detected between polys and mesh\n\n";
    }
    off = meshOffset;
    if(IS_SEGMENTED(off)) {
        off = SEGMENT_OFFSET(off);
    }
    defaultMeshSymbol << symbol << "_mesh_" << std::uppercase << std::hex << off;
    auto meshSymbol = GetSafeNode(node, "mesh_symbol", defaultMeshSymbol.str());

    if (meshSymbol.find("OFFSET") != std::string::npos) {
        std::ostringstream offsetSeg;

        offsetSeg << std::uppercase << std::hex << meshOffset;
        meshSymbol = std::regex_replace(meshSymbol, std::regex(R"(OFFSET)"), offsetSeg.str());
    }

    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::uppercase << std::hex << off << "\n";
    }

    write << "Vec3s " << meshSymbol << "[] = {";
    i = 0;
    for(Vec3s vtx : colpolys->mMesh) {
        if((i++ % 4) == 0) {
            write << "\n" << fourSpaceTab;
        }
        write << NUM(vtx, 6) << ",   ";
    }

    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// Mesh vertex count: " << colpolys->mMesh.size() << "\n";
    }

    return (IS_SEGMENTED(offset) ? SEGMENT_OFFSET(offset) : offset) + colpolys->mMesh.size() * sizeof(Vec3s);
}

ExportResult SF64::ColPolyBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SF64::ColPolyFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto count = GetSafeNode<uint32_t>(node, "count");
    std::vector<SF64::CollisionPoly> polys;
    std::vector<Vec3s> mesh;
    int meshSize = 0;
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, count * sizeof(SF64::CollisionPoly));
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);

    for(int i = 0; i < count; i++) {
        int16_t v0 = reader.ReadInt16();
        meshSize = std::max(meshSize, (int)v0);
        int16_t v1 = reader.ReadInt16();
        meshSize = std::max(meshSize, (int)v1);
        int16_t v2 = reader.ReadInt16();
        meshSize = std::max(meshSize, (int)v2);
        int16_t pad1 = reader.ReadInt16();
        int16_t nx = reader.ReadInt16();
        int16_t ny = reader.ReadInt16();
        int16_t nz = reader.ReadInt16();
        int16_t pad2 = reader.ReadInt16();
        int16_t dist = reader.ReadInt32();

        polys.push_back(CollisionPoly({{v0, v1, v2}, pad1, {nx, ny, nz}, pad2, dist}));
    }
    meshSize++;
    const auto meshOffset = GetSafeNode<uint32_t>(node, "mesh_offset", offset + count * sizeof(SF64::CollisionPoly));
    YAML::Node meshNode;
    meshNode["offset"] = meshOffset;
    auto [__, meshSegment] = Decompressor::AutoDecode(meshNode, buffer, meshSize * sizeof(Vec3s));
    LUS::BinaryReader meshReader(meshSegment.data, meshSegment.size);
    meshReader.SetEndianness(LUS::Endianness::Big);

    for(int i = 0; i < meshSize; i++) {
        Vec3s vtx;

        vtx.x = meshReader.ReadInt16();
        vtx.y = meshReader.ReadInt16();
        vtx.z = meshReader.ReadInt16();
        mesh.push_back(vtx);
    }

    return std::make_shared<SF64::ColPolyData>(polys, mesh);
}
