#include "BlobFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <iomanip>

ExportResult BlobCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    write << GetSafeNode<std::string>(node, "ctype", "u8") << " " << symbol << "[] = {\n" << tab;

    for (int i = 0; i < data.size(); i++) {
        if ((i % 15 == 0) && i != 0) {
            write << "\n" << tab;
        }

        write << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int) data[i] << ", ";
    }
    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// size: 0x" << std::hex << std::uppercase << data.size() << "\n";
    }

    return offset + data.size();
}

ExportResult BlobBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    WriteHeader(writer, LUS::ResourceType::Blob, 0);
    writer.Write((uint32_t) data.size());
    writer.Write((char*) data.data(), data.size());
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> BlobFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto size = GetSafeNode<size_t>(node, "size");
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    return std::make_shared<RawBuffer>(segment.data, segment.size);
}