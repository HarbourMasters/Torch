#include "Decompressor.h"

#include <stdexcept>
#include "spdlog/spdlog.h"
#include <Companion.h>

extern "C" {
#include <libmio0/mio0.h>
}

std::unordered_map<uint32_t, DataChunk*> gCachedChunks;

DataChunk* Decompressor::Decode(const std::vector<uint8_t>& buffer, const uint32_t offset, const CompressionType type) {

    if(gCachedChunks.contains(offset)){
        return gCachedChunks[offset];
    }

    const unsigned char* in_buf = buffer.data() + offset;

	switch (type) {
        case CompressionType::MIO0: {
            mio0_header_t head;
            if(!mio0_decode_header(in_buf, &head)){
                throw std::runtime_error("Failed to decode MIO0 header");
            }

            const auto decompressed = new uint8_t[head.dest_size];
            mio0_decode(in_buf, decompressed, nullptr);
            gCachedChunks[offset] = new DataChunk{ decompressed, head.dest_size };
            return gCachedChunks[offset];
        }
        default:
            throw std::runtime_error("Unknown compression type");
    }
}

DecompressedData Decompressor::AutoDecode(YAML::Node& node, std::vector<uint8_t>& buffer, std::optional<size_t> manualSize) {
    if(!node["offset"]){
        throw std::runtime_error("Failed to find offset");
    }

    auto offset = node["offset"].as<uint32_t>();

    // Header of compressed file; MIO0, YAY0, or YAZ0.
    std::string header = "";

    const auto compressed_offset = Companion::Instance->GetCompressedOffset();

    auto type = CompressionType::None;

    // Compression type known
    if (node["compression"]["type"]) {
        type = static_cast<CompressionType>(node["compression"]["type"].as<uint32_t>());
    } else { // Find compression type
        // Get signature of compressed data; MIO0, YAY0, YAZ0, etc.
        if (compressed_offset.has_value()) {
            LUS::BinaryReader reader((char*) buffer.data() + compressed_offset.value(), sizeof(uint32_t));
            reader.SetEndianness(LUS::Endianness::Big);

            header = reader.ReadCString();

            // Check if a compressed header exists
            if (header == "MIO0") {
                type = CompressionType::MIO0;
            } else if (header == "YAY0") {
                type = CompressionType::YAY0;
            } else if (header == "YAZ0") {
                type = CompressionType::YAZ0;
            } else {
                throw std::runtime_error("Unknown compression signature; expected MIO0, YAY0, or YAZ0.");
            }
        }
    }

    // Process compressed assets
    switch(type) {
        case CompressionType::MIO0:
        {
            if (compressed_offset.has_value()) {
                node["compression"]["type"] = (uint32_t)  CompressionType::MIO0;
                offset = IS_SEGMENTED(offset) ? SEGMENT_OFFSET(offset) : offset;

                    auto decoded = Decode(buffer, compressed_offset.value(), CompressionType::MIO0);
                auto size = node["size"] ? node["size"].as<size_t>() : manualSize.value_or(decoded->size - offset);
                return {
                    .root = decoded,
                    .segment = { decoded->data + offset, size }
                };
            }
            throw std::runtime_error("Received an incorrect compressed offset while trying to extract an asset.");
        }
        case CompressionType::YAY0:
            node["compression"]["type"] = (uint32_t) CompressionType::YAY0;
            throw std::runtime_error("Found compressed yay0 segment.\nDecompression of yay0 has not been implemented yet.");
            break;
        case CompressionType::YAZ0:
            node["compression"]["type"] = (uint32_t) CompressionType::YAZ0;
            throw std::runtime_error("Found compressed yaz0 segment.\nDecompression of yaz0 has not been implemented yet.");
            break;
        default:
            SPDLOG_DEBUG("Unknown compression type for asset. If this is compressed data something has gone wrong. Else, all is fine.");

    }

    offset = TranslateAddr(offset);

    // Uncompressed
    return {
        .root = nullptr,
        .segment = { buffer.data() + offset, node["size"] ? node["size"].as<size_t>() : manualSize.value_or(buffer.size() - offset) }
    };
}

uint32_t Decompressor::TranslateAddr(uint32_t addr, bool baseAddress){
    if(IS_SEGMENTED(addr)){
        const auto segment = Companion::Instance->GetSegmentedAddr(SEGMENT_NUMBER(addr));
        if(!segment.has_value()) {
            SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", SEGMENT_NUMBER(addr));
            return 0;
        }

        return segment.value() + (!baseAddress ? SEGMENT_OFFSET(addr) : 0);
    }

    return addr;
}

bool Decompressor::IsSegmented(uint32_t addr) {
    if(IS_SEGMENTED(addr)){
        const auto segment = Companion::Instance->GetSegmentedAddr(SEGMENT_NUMBER(addr));

        if(!segment.has_value()) {
            SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", SEGMENT_NUMBER(addr));
            return false;
        }

        return true;
    }

    return false;
}

void Decompressor::CopyCompression(YAML::Node& source, YAML::Node& dest){
    if (source["compression"]["type"]) {
        dest["compression"]["type"] = source["compression"]["type"];
    }
}

void Decompressor::ClearCache() {
    for(auto& [key, value] : gCachedChunks){
        delete value->data;
    }
    gCachedChunks.clear();
}
