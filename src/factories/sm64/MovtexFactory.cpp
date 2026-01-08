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
    uint32_t additionalSize = 0;

    write << "static Movtex " << symbol << "[] = {\n";

    if (movtex->mIsQuad) {
        auto numLists = movtex->mMovtexData.at(0);
        write << fourSpaceTab << "MOV_TEX_INIT_LOAD(" << numLists << "),\n";
        additionalSize += 2; // Count + Padding
        for (uint32_t i = 0; i < numLists; ++i) {
            uint32_t base = 1 + (i * 14);
            write << fourSpaceTab << "MOV_TEX_ROT_SPEED(" << movtex->mMovtexData.at(base + 0) << "),\n";
            write << fourSpaceTab << "MOV_TEX_ROT_SCALE(" << movtex->mMovtexData.at(base + 1) << "),\n";
            write << fourSpaceTab << "MOV_TEX_4_BOX_TRIS(" << movtex->mMovtexData.at(base + 2) << ", " << movtex->mMovtexData.at(base + 3) << "),\n";
            write << fourSpaceTab << "MOV_TEX_4_BOX_TRIS(" << movtex->mMovtexData.at(base + 4) << ", " << movtex->mMovtexData.at(base + 5) << "),\n";
            write << fourSpaceTab << "MOV_TEX_4_BOX_TRIS(" << movtex->mMovtexData.at(base + 6) << ", " << movtex->mMovtexData.at(base + 7) << "),\n";
            write << fourSpaceTab << "MOV_TEX_4_BOX_TRIS(" << movtex->mMovtexData.at(base + 8) << ", " << movtex->mMovtexData.at(base + 9) << "),\n";
            write << fourSpaceTab << "MOV_TEX_ROT(" << movtex->mMovtexData.at(base + 10) << "),\n";
            write << fourSpaceTab << "MOV_TEX_ALPHA(" << movtex->mMovtexData.at(base + 11) << "),\n";
            write << fourSpaceTab << "MOV_TEX_DEFINE(" << movtex->mMovtexData.at(base + 12) << "),\n";
            write << fourSpaceTab << "MOV_TEX_END(),\n";
            additionalSize += 10;
        }
    } else {
        write << fourSpaceTab << "MOV_TEX_SPEED(" << movtex->mMovtexData.at(0) << "),\n";
        additionalSize += 1;
        if (movtex->mHasColor) {
            for (uint32_t i = 0; i < movtex->mVertexCount; ++i) {
                // There is also a MOV_TEX_LIGHT_TRIS macro, however this is identical in result to using MOV_TEX_ROT_TRIS and would require more params on the yaml
                write << fourSpaceTab << "MOV_TEX_ROT_TRIS(";
                write << movtex->mMovtexData.at(i * 8 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 2) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 3) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 4) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 5) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 6) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 7) << ", ";
                write << movtex->mMovtexData.at(i * 8 + 8) << "),\n";
                additionalSize += 8;
            }
        } else {
            for (uint32_t i = 0; i < movtex->mVertexCount; ++i) {
                write << fourSpaceTab << "MOV_TEX_TRIS(";
                write << movtex->mMovtexData.at(i * 5 + 1) << ", ";
                write << movtex->mMovtexData.at(i * 5 + 2) << ", ";
                write << movtex->mMovtexData.at(i * 5 + 3) << ", ";
                write << movtex->mMovtexData.at(i * 5 + 4) << ", ";
                write << movtex->mMovtexData.at(i * 5 + 5) << "),\n";
                additionalSize += 5;
            }
        }
        write << fourSpaceTab << "MOV_TEX_END(),\n";
        additionalSize += 1;
    }

    write << "};\n";

    return offset + (additionalSize * sizeof(int16_t));
}

ExportResult SM64::MovtexBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto movtex = std::static_pointer_cast<SM64::MovtexData>(raw);

    WriteHeader(writer, Torch::ResourceType::Movtex, 0);

    std::vector<int16_t> buffer;

    if (movtex->mIsQuad) {
        auto numLists = movtex->mMovtexData.at(0);
        
        buffer.push_back(numLists);
        buffer.push_back(0); // Alignment padding
        
        for (uint32_t i = 0; i < numLists; ++i) {
            for(size_t j = 0; j < 14; j++){
                buffer.push_back(movtex->mMovtexData.at(1 + (i * 14) + j));
            }
        }
    } else {
        buffer.push_back(movtex->mMovtexData.at(0));
        
        size_t triSize = movtex->mHasColor ? 8 : 5;
        for (uint32_t i = 0; i < movtex->mVertexCount * triSize; ++i) {
            buffer.push_back(movtex->mMovtexData.at(1 + i));
        }
        
        buffer.push_back(0); // MOV_TEX_END
    }

    writer.Write((uint32_t) buffer.size());
    writer.Write((char*) buffer.data(), buffer.size() * sizeof(int16_t));

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
            count = GetSafeNode<uint32_t>(node, "count");
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
    reader.SetEndianness(Torch::Endianness::Big);

    std::vector<int16_t> movtexData;

    if (isQuad) {
        auto numLists = reader.ReadInt16();
        reader.ReadInt16(); // pad
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
