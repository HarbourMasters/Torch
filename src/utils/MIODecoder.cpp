#include "MIODecoder.h"

extern "C" {
#include <libmio0/mio0.h>
}
#include <stdexcept>

std::unordered_map<uint32_t, std::vector<char>> MIO0Decoder::gCachedChunks;

std::vector<char>& MIO0Decoder::Decode(std::vector<uint8_t>& buffer, uint32_t offset) {
    const unsigned char* in_buf = buffer.data() + offset;

	mio0_header_t head;
	if(!mio0_decode_header(in_buf, &head)){
        throw std::runtime_error("Failed to decode MIO0 header");
    }

    uint8_t* decompressed = new uint8_t[head.dest_size];
	mio0_decode(in_buf, decompressed, nullptr);
    gCachedChunks[offset] = std::vector<char>(decompressed, decompressed + head.dest_size);

    delete[] decompressed;
    return gCachedChunks[offset];
}

void MIO0Decoder::ClearCache() {
    gCachedChunks.clear();
}