#include "BlobFactory.h"
#include <filesystem>
#include "utils/MIODecoder.h"

namespace fs = std::filesystem;

bool BlobFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {

	WRITE_HEADER(LUS::ResourceType::Blob, 0);

	auto size = data["size"].as<size_t>();
	auto offset = data["offset"].as<size_t>();

	auto* blob = new uint8_t[size];
	if(data["mio0"]){
		auto decoded = MIO0Decoder::Decode(buffer, offset);
        auto cursor = data["mio0"].as<size_t>();
        memcpy(blob, decoded.data() + cursor, size);
	} else {
    	memcpy(blob, buffer.data() + offset, size);
	}

	WRITE_U32(size); // Blob Data Size
	WRITE_ARRAY(blob, size); // Blob Data

	delete[] blob;
	return true;
}
