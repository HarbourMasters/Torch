#include "MovtexFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include <regex>

ExportResult SM64::MovtexHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Movtex " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult SM64::MovtexCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");

    auto movtex = std::static_pointer_cast<SM64::MovtexData>(raw);

    write << "static Movtex " << symbol << "[] = {";

    if (movtex->mIsQuad) {
        auto numLists = movtex->mMovtexData.at(0);
        write << "MOV_TEX_INIT_LOAD(" << numLists << "),\n";
        for (int16_t i = 0; i < numLists; ++i) {
            write << "MOV_TEX_ROT_SPEED(" << movtex->mMovtexData.at(i * 14 + 1) << "),\n";
            write << "MOV_TEX_ROT_SCALE(" << movtex->mMovtexData.at(i * 14 + 2) << "),\n";
            write << "MOV_TEX_4_BOX_TRIS(" << movtex->mMovtexData.at(i * 14 + 3) << ", " << movtex->mMovtexData.at(i * 14 + 4) << "),\n";
            write << "MOV_TEX_4_BOX_TRIS(" << movtex->mMovtexData.at(i * 14 + 5) << ", " << movtex->mMovtexData.at(i * 14 + 6) << "),\n";
            write << "MOV_TEX_4_BOX_TRIS(" << movtex->mMovtexData.at(i * 14 + 7) << ", " << movtex->mMovtexData.at(i * 14 + 8) << "),\n";
            write << "MOV_TEX_4_BOX_TRIS(" << movtex->mMovtexData.at(i * 14 + 9) << ", " << movtex->mMovtexData.at(i * 14 + 10) << "),\n";
            write << "MOV_TEX_ROT(" << movtex->mMovtexData.at(i * 14 + 11) << "),\n";
            write << "MOV_TEX_ALPHA(" << movtex->mMovtexData.at(i * 14 + 12) << "),\n";
            write << "MOV_TEX_DEFINE(" << movtex->mMovtexData.at(i * 14 + 13) << "),\n";
            write << "MOV_TEX_END(),\n";
        }
    } else {
        write << "MOV_TEX_SPEED(" << movtex->mMovtexData.at(0) << "),\n";
        if (movtex->mHasColor) {
            for (uint32_t i = 0; i < movtex->mVertexCount; ++i) {
                // There is also a MOV_TEX_LIGHT_TRIS macro, however this is identical in result to using MOV_TEX_ROT_TRIS and would require more params on the yaml
                write << "MOV_TEX_ROT_TRIS(";
                write << movtex->mMovtexData.at(i * 8 + 0 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 1 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 2 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 3 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 4 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 5 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 6 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 7 + 1) << "),\n";
            }
        } else {
            for (uint32_t i = 0; i < movtex->mVertexCount; ++i) {
                write << "MOV_TEX_TRIS(";
                write << movtex->mMovtexData.at(i * 8 + 0 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 1 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 2 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 3 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 4 + 1) << "),\n";
            }
        }
        write << "MOV_TEX_END(),\n";
    }

    write << "\n};\n";

    size_t size = (movtex->mMovtexData.size() + 1) * sizeof(int16_t);

    return offset + size;
}

ExportResult SM64::MovtexBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto movtex = std::static_pointer_cast<SM64::MovtexData>(raw);

    WriteHeader(writer, LUS::ResourceType::Movtex, 0);

    // May want extra info, but importer should just be mMovtexData with an extra 0 (or 2) at the end
    // if (movtex->mIsQuad) {
    //     writer.Write(0);
    //     writer.Write(0);
    // } else {
    //     writer.Write(movtex->mVertexCount);
    //     writer.Write((uint32_t)(movtex->mHasColor ? 1 : 0));
    // }
    
    for (auto datum : movtex->mMovtexData) {
        writer.Write(datum);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SM64::MovtexFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    bool isQuad = false;
    uint32_t count = 0;
    bool hasColor = false;

    // If quad we can generate automatically
    if (node["quad"]) {
        isQuad = GetSafeNode<bool>(node, "quad");
    }
    // Otherwise we need to know the count of vertices and the colour attribute
    if (!isQuad) {
        if (node["count"]) {
            count = GetSafeNode<uint32_t>(node, "quad");
        } else {
            SPDLOG_WARN("Non quad movtex needs count field");
        }
        if (node["has_color"]) {
            hasColor = GetSafeNode<bool>(node, "has_color");
        } else {
            SPDLOG_WARN("Non quad movtex needs has_color field");
        }
    }
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);

    std::vector<int16_t> movtexData;

    if (isQuad) {
        auto numLists = reader.ReadInt16();
        movtexData.emplace_back(numLists);
        // movtex quad data has 14 elements
        for (int16_t i = 0; i < numLists * 14; ++i) {
            movtexData.emplace_back(reader.ReadInt16());
        }
        if (reader.ReadInt16() != 0) {
            SPDLOG_WARN("Incorrect end to movtex quad found at symbol: {}, offset: 0x{:X}", symbol, offset);
        }
    } else {
        auto speed = reader.ReadInt16();
        movtexData.emplace_back(speed);
        size_t triSize = hasColor ? 8 : 5;
        for (size_t i = 0; i < count * triSize; ++i) {
            movtexData.emplace_back(reader.ReadInt16());
        }

        if (reader.ReadInt16() != 0) {
            SPDLOG_WARN("Incorrect end to movtex object found at symbol: {}, offset: 0x{:X}", symbol, offset);
        }
    }

    return std::make_shared<SM64::MovtexData>(movtexData, isQuad, count, hasColor);
}
