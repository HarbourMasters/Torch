#include "BlobFactory.h"

#include <iostream>
#include <filesystem>
#include "Companion.h"
#include "utils/MIODecoder.h"

namespace fs = std::filesystem;

bool BlobFactory::process(LUS::BinaryWriter* writer, nlohmann::json& data, std::vector<uint8_t>& buffer) {
    auto metadata = data["offsets"];
	if(!metadata[1].contains("us")){
		return false;
	}

    WRITE_HEADER(this->type, 0);

	size_t size = metadata[0];
	auto offsets = metadata[1]["us"];

	auto* blob = new uint8_t[size];
	if(this->isMIOChunk){
		auto mio0 = MIO0Decoder::Decode(buffer, offsets[0]);
        memcpy(blob, mio0.data() + offsets[1], size);
	} else {
    	memcpy(blob, buffer.data() + offsets[0], size);
	}

	WRITE_U32(size); // Blob Data Size
	WRITE_ARRAY(blob, size); // Blob Data

	delete[] blob;
	return true;
}