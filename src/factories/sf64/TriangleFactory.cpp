#include "TriangleFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"


#define NUM(x, w) std::dec << std::setfill(' ') << std::setw(w) << x
// #define FLOAT(x, w) std::dec << std::setfill(' ') << (((int) x == x) ? "" : "  ") << std::setw(w - 2) << x << (((int) x == x) ? ".0f" : "f")

SF64::TriangleData::TriangleData(std::vector<Vec3s> tris, std::vector<std::vector<Vec3f>> meshes): mTris(tris), mMeshes(meshes) {
    
}

void SF64::TriangleHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern Triangle " << symbol << "[];\n";
}

void SF64::TriangleCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto triData = std::static_pointer_cast<SF64::TriangleData>(raw);
    auto off = offset;
    int i;
    if(IS_SEGMENTED(off)) {
        off = SEGMENT_OFFSET(off);
    } 
    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::uppercase << std::hex << off << "\n";
    }

    write << "Triangle " << symbol << "[] = {";
    int width = std::log10(triData->mMeshes[0].size()) + 1;
    i = 0;
    for(Vec3s tri : triData->mTris) {
        if((i++ % 6) == 0) {
            write << "\n" << fourSpaceTab;
        }
        write << NUM(tri, width) << ", ";
    }

    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// Triangle count: " << triData->mTris.size() << "\n";
        write << "// 0x" << std::uppercase << std::hex << off + triData->mTris.size() * sizeof(Vec3s) << "\n";
    }

    write << "\n";

    
    auto meshOffset = offset + triData->mTris.size() * sizeof(Vec3s);
    off = meshOffset;
    if(IS_SEGMENTED(off)) {
        off = SEGMENT_OFFSET(off);
    }
    for(int j = 0; j < triData->mMeshes.size(); j++) {
        std::string indexTag = "";
        std::ostringstream defaultMeshSymbol;
        if(triData->mMeshes.size() != 1) {
            indexTag = std::to_string(j) + "_";
        }
        defaultMeshSymbol << symbol << "_mesh_" << indexTag << std::uppercase << std::hex << off;
        const auto meshSymbol = GetSafeNode(node, "mesh_symbol", defaultMeshSymbol.str());
        if (Companion::Instance->IsDebug()) {
            write << "// 0x" << std::uppercase << std::hex << off << "\n";
        }

        write << "Vec3f " << meshSymbol << "[] = {";
        i = 0;
        for(Vec3f vtx : triData->mMeshes[j]) {
            if((i++ % 4) == 0) {
                write << "\n" << fourSpaceTab;
            }
            write << NUM(vtx, 5) << ",   ";
        }

        write << "\n};\n";
        off += triData->mMeshes[j].size() * sizeof(Vec3f);
        if (Companion::Instance->IsDebug()) {
            write << "// Mesh vertex count: " << triData->mMeshes[j].size() << "\n";
            write << "// 0x" << std::uppercase << std::hex << off << "\n";
        }
        write << "\n";
    }

    

}

void SF64::TriangleBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {

}

std::optional<std::shared_ptr<IParsedData>> SF64::TriangleFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto count = GetSafeNode<uint32_t>(node, "count");
    const auto meshCount = GetSafeNode<uint32_t>(node, "mesh_count", 1);
    std::vector<Vec3s> tris;
    int meshSize = 0;
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, count * sizeof(Vec3s));
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);

    for(int i = 0; i < count; i++) {
        Vec3s tri;

        tri.x = reader.ReadInt16();
        meshSize = std::max(meshSize, (int)tri.x);
        tri.y = reader.ReadInt16();
        meshSize = std::max(meshSize, (int)tri.y);
        tri.z = reader.ReadInt16();
        meshSize = std::max(meshSize, (int)tri.z);

        tris.push_back(tri);
    }
    meshSize++;
    const auto meshOffset = GetSafeNode<uint32_t>(node, "mesh_offset", offset + count * sizeof(Vec3s));
    YAML::Node meshNode;
    meshNode["offset"] = meshOffset;
    auto [__, meshSegment] = Decompressor::AutoDecode(meshNode, buffer, meshSize * meshCount * sizeof(Vec3f));
    LUS::BinaryReader meshReader(meshSegment.data, meshSegment.size);
    meshReader.SetEndianness(LUS::Endianness::Big);
    std::vector<std::vector<Vec3f>> meshes;
    for(int j = 0; j < meshCount; j++) {
        std::vector<Vec3f> mesh;
        for(int i = 0; i < meshSize; i++) {
            mesh.push_back(Vec3f(meshReader.ReadFloat(), meshReader.ReadFloat(), meshReader.ReadFloat()));
        }
        meshes.push_back(mesh);
    }

    return std::make_shared<SF64::TriangleData>(tris, meshes);
}
