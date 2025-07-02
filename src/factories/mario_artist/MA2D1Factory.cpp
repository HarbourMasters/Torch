#include "MA2D1Factory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

ExportResult MA::MA2D1HeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    return std::nullopt;
}

ExportResult MA::MA2D1CodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto data = std::static_pointer_cast<MA2D1Data>(raw);

    write << "char " << symbol << "_header" << "[] = { ";

    for (size_t i = 0; i < data->mFormat.size(); i++) {
        if (i != 0) {
            write << ", ";
        }

        write << "\'" << data->mFormat.at(i) << "\'";
    }

    write << ", \'" << ((data->mWidth / 100) % 10) << "\'";
    write << ", \'" << ((data->mWidth / 10) % 10) << "\'";
    write << ", \'" << ((data->mWidth / 1) % 10) << "\'";

    write << ", \'" << ((data->mHeight / 100) % 10) << "\'";
    write << ", \'" << ((data->mHeight / 10) % 10) << "\'";
    write << ", \'" << ((data->mHeight / 1) % 10) << "\'";

    write << ", \'" << ((data->mSize / 100000) % 10) << "\'";
    write << ", \'" << ((data->mSize / 10000) % 10) << "\'";
    write << ", \'" << ((data->mSize / 1000) % 10) << "\'";
    write << ", \'" << ((data->mSize / 100) % 10) << "\'";
    write << ", \'" << ((data->mSize / 10) % 10) << "\'";
    write << ", \'" << ((data->mSize / 1) % 10) << "\'";

    write << " };\n\n";

    return offset + 0x10;
}

ExportResult MA::MA2D1BinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    // Nothing Required Here For Binary Exporting

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> MA::MA2D1Factory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    LUS::BinaryReader reader(segment.data, segment.size);

    reader.SetEndianness(Torch::Endianness::Big);

    YAML::Node thumbnail;
    thumbnail["type"] = "TEXTURE";
    thumbnail["ctype"] = "u16";
    thumbnail["format"] = "RGBA16";
    thumbnail["width"] = 24;
    thumbnail["height"] = 24;
    thumbnail["offset"] = offset;
    thumbnail["symbol"] = symbol + "_thumb";
    Companion::Instance->AddAsset(thumbnail);

    reader.Seek(0x480, LUS::SeekOffsetType::Start);

    char headerBuffer[0x10];

    for (size_t i = 0; i < sizeof(headerBuffer); i++) {
        headerBuffer[i] = reader.ReadChar();
    }

    // Override offset
    node["offset"] = offset + 0x480;

    std::string format(headerBuffer, headerBuffer + 4);

    uint32_t width = std::stoi(std::string(headerBuffer + 4, 3));
    uint32_t height = std::stoi(std::string(headerBuffer + 7, 3));
    uint32_t size = std::stoi(std::string(headerBuffer + 10, 6));

    YAML::Node image;
    if (format == "NCMP") {
        image["type"] = "COMPRESSED_TEXTURE";
        image["compression"] = "YAY1";
    } else {
        image["type"] = "TEXTURE";
    }

    image["ctype"] = "u16";
    image["format"] = "RGBA16";
    image["width"] = width;
    image["height"] = height;
    image["offset"] = offset + 0x490;
    image["symbol"] = symbol + "_image";
    Companion::Instance->AddAsset(image);

    return std::make_shared<MA2D1Data>(format, width, height, size);
}
