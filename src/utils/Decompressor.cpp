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

    if(node["mio0"]){
        const auto mio0 = node["mio0"].as<uint32_t>();

        offset = TranslateAddr(offset, IS_SEGMENTED(offset));        

        auto decoded = Decode(buffer, offset, CompressionType::MIO0);
        auto size = node["size"] ? node["size"].as<size_t>() : manualSize.value_or(decoded->size - mio0);

        return {
            .root = decoded,
            .segment = { decoded->data + mio0, size }
        };
    }

    if(node["yay0"]){
        throw std::runtime_error("Yay0 not implemented");
    }

    if(node["yaz0"]){
        throw std::runtime_error("Yaz0 not implemented");
    }

    offset = TranslateAddr(offset);

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

bool Decompressor::IsCompressed(YAML::Node& node) {
    return node["mio0"] || node["yay0"] || node["yaz0"];
}

void Decompressor::CopyCompression(YAML::Node& from, YAML::Node& to){
    if(from["mio0"]){
        to["mio0"] = from["mio0"];
    }

    if(from["yay0"]){
        to["yay0"] = from["yay0"];
    }

    if(from["yaz0"]){
        to["yaz0"] = from["yaz0"];
    }
}

void Decompressor::ClearCache() {
    for(auto& [key, value] : gCachedChunks){
        delete value->data;
    }
    gCachedChunks.clear();
}
