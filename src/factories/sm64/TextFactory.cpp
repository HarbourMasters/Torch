#include "TextFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/MIODecoder.h"


void SM64::TextBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    WriteHeader(writer, LUS::ResourceType::Blob, 0);
    writer.Write((uint32_t) data.size());
    writer.Write((char*) data.data(), data.size());
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SM64::TextFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    VERIFY_ENTRY(data, "mio0", "yaml missing entry for mio0.\nEx. mio0: 0x10100")
    VERIFY_ENTRY(data, "offset", "yaml missing entry for offset.\nEx. offset: 0x100")

    auto offset = data["offset"].as<size_t>();
    auto mio0 = data["mio0"].as<size_t>();

    std::vector<uint8_t> text;
    auto decoded = MIO0Decoder::Decode(buffer, mio0);
    auto bytes = (uint8_t*) decoded.data();

    while(bytes[offset] != 0xFF){
        auto c = bytes[offset++];
        text.push_back(c);
    }
    text.push_back(0xFF);

    return std::make_shared<RawBuffer>(text);
}