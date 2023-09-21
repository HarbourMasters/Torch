#include "MIO0Factory.h"
#include "utils/MIODecoder.h"

namespace fs = std::filesystem;

bool MIO0Factory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {

//	WRITE_HEADER(LUS::ResourceType::Blob, 0);

	auto offset = data["offset"].as<size_t>();

    auto decoded = MIO0Decoder::Decode(buffer, offset);
    auto size = decoded.size();
    auto* blob = new uint8_t[size];
    memcpy(blob, decoded.data(), size);

//	WRITE_U32(size); // MIO0 Data Size
	WRITE_ARRAY(blob, size); // MIO0 Data

	delete[] blob;
	return true;
}
