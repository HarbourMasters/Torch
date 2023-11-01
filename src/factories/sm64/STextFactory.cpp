#include "STextFactory.h"

#include "utils/MIODecoder.h"

namespace fs = std::filesystem;

bool STextFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {

    WRITE_HEADER(LUS::ResourceType::Blob, 0);

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

    WRITE_U32(text.size());
    WRITE_ARRAY(text.data(), text.size());

	return true;
}
