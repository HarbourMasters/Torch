#include "BlobFactory.h"
#include "utils/MIODecoder.h"
#include <iomanip>

void BlobCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    write << "u8 " << entryName << "[] = {\n" << tab;
    for (int i = 0; i < data.size(); i++) {
        if ((i % 15 == 0) && i != 0) {
            write << "\n" << tab;
        }

        write << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int) data[i] << ", ";
    }
    write << "\n};\n";
}

void BlobBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    WriteHeader(writer, LUS::ResourceType::Blob, 0);
    writer.Write((uint32_t) data.size());
    writer.Write((char*) data.data(), data.size());
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> BlobFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    auto size = data["size"].as<uint32_t>();
	auto offset = data["offset"].as<uint32_t>();
    std::shared_ptr<RawBuffer> result;

	if(data["mio0"]){
		auto decoded = MIO0Decoder::Decode(buffer, offset);
        auto cursor = data["mio0"].as<uint32_t>();
        result = std::make_shared<RawBuffer>(decoded.data() + cursor, size);
	} else {
        result = std::make_shared<RawBuffer>(buffer.data() + offset, size);
	}

    return result;
}