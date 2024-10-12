#include "BlobFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <iomanip>

void BlobCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    if (Companion::Instance->IsDebug()) {
        if (IS_SEGMENTED(offset)) {
            offset = SEGMENT_OFFSET(offset);
        }
        write << "// 0x" << std::hex << std::uppercase << offset << "\n";
    }

    if(node["ctype"]) {
        write << node["ctype"].as<std::string>() << " " << symbol << "[] = {\n" << tab;
    } else {
        write << "u8 " << symbol << "[] = {\n" << tab;
    }

    for (int i = 0; i < data.size(); i++) {
        if ((i % 15 == 0) && i != 0) {
            write << "\n" << tab;
        }

        write << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int) data[i] << ", ";
    }
    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        const auto sz = data.size();

        write << "// size: 0x" << std::hex << std::uppercase << sz << "\n";
        if (IS_SEGMENTED(offset)) {
            offset = SEGMENT_OFFSET(offset);
        }
        write << "// 0x" << std::hex << std::uppercase << (offset + sz) << "\n";
    }

    write << "\n";
}

void BlobBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    WriteHeader(writer, LUS::ResourceType::Blob, 0);
    writer.Write((uint32_t) data.size());
    writer.Write((char*) data.data(), data.size());
    writer.Finish(write);
}

void BlobModdingExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();

    *replacement += ".bin";

    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;
    writer.Write((char*) data.data(), data.size());
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> BlobFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto size = GetSafeNode<size_t>(node, "size");
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    return std::make_shared<RawBuffer>(segment.data, segment.size);
}