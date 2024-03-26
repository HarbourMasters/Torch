#include "TriangleFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include <regex>


#define NUM(x, w) std::dec << std::setfill(' ') << std::setw(w) << x
#define FORMAT_FLOAT(x, w, p) std::dec << std::setfill(' ') << std::fixed << std::setprecision(p) << std::setw(w) << x

SF64::TriangleData::TriangleData(std::vector<Vec3s> tris, std::vector<YAML::Node> meshNodes): mTris(tris), mMeshNodes(meshNodes) {
}

ExportResult SF64::TriangleHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Triangle " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult SF64::TriangleCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto triData = std::static_pointer_cast<SF64::TriangleData>(raw);
    const auto meshSize = GetSafeNode(triData->mMeshNodes[0], "count", 0);
    auto segment = 0;
    int width = std::log10(meshSize) + 1;
    int i = 0;

    write << "Triangle " << symbol << "[] = {";
    for(Vec3s tri : triData->mTris) {
        if((i++ % 6) == 0) {
            write << "\n" << fourSpaceTab;
        }
        write << NUM(tri, width) << ", ";
    }

    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// Triangle count: " << triData->mTris.size() << "\n";
    }

    return offset + triData->mTris.size() * sizeof(Vec3s);
}

ExportResult SF64::TriangleBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SF64::TriangleFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto count = GetSafeNode<uint32_t>(node, "count");
    const auto meshCount = GetSafeNode<uint32_t>(node, "mesh_count", 1);
    std::vector<Vec3s> tris;
    std::vector<YAML::Node> meshNodes;
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
    auto meshOffset = GetSafeNode<uint32_t>(node, "mesh_offset", offset + count * sizeof(Vec3s));
    for(int j = 0; j < meshCount; j++) {
        YAML::Node meshNode;

        if(node["mesh_symbol"]) {
            auto meshSymbol = GetSafeNode<std::string>(node, "mesh_symbol");
            if (meshSymbol.find("OFFSET") == std::string::npos) {
                if(meshCount > 1) {
                    meshSymbol += "_" + std::to_string(j);
                }
            } else {
                std::ostringstream offsetSeg;
                std::string replaceOff = "OFFSET";
                std::string off;
                offsetSeg << std::uppercase << std::hex << meshOffset;
                off = offsetSeg.str();
                meshSymbol = std::regex_replace(meshSymbol, std::regex(R"(OFFSET)"), offsetSeg.str());
            }
            meshNode["symbol"] = meshSymbol;
        }
        meshNode["type"] = "VEC3F";
        meshNode["count"] = meshSize;
        meshNode["offset"] = meshOffset;

        meshNode = Companion::Instance->AddAsset(meshNode).value();
        meshNodes.push_back(meshNode);
        meshOffset += meshSize * sizeof(Vec3f);
    }

    return std::make_shared<SF64::TriangleData>(tris, meshNodes);
}
