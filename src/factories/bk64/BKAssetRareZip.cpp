#include <vector>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <utility>
#include "zlib.h"
#include "BKAssetRareZip.h"

namespace BK64 {

/**
 * Implementation of bk_unzip as declared in bk_unzip.h
 */
std::vector<uint8_t> bk_unzip(const uint8_t* in_buffer, size_t in_size) {
    // Check buffer size
    if (in_size < 6) {
        throw std::runtime_error("Input buffer too small");
    }
    
    // Check for BK magic header
    if (in_buffer[0] != 0x11 || in_buffer[1] != 0x72) {
        throw std::runtime_error("Input buffer does not have BK header");
    }
    
    // Extract expected uncompressed length (big-endian)
    uint32_t expected_len = 
        (static_cast<uint32_t>(in_buffer[2]) << 24) |
        (static_cast<uint32_t>(in_buffer[3]) << 16) |
        (static_cast<uint32_t>(in_buffer[4]) << 8) |
        static_cast<uint32_t>(in_buffer[5]);
    
    // Prepare output buffer - make it slightly larger to handle any potential overflow
    std::vector<uint8_t> out_buffer(expected_len + 1024);
    
    // Set up zlib stream
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = static_cast<uInt>(in_size - 6);
    stream.next_in = const_cast<Bytef*>(in_buffer + 6);
    stream.avail_out = static_cast<uInt>(out_buffer.size());
    stream.next_out = out_buffer.data();
    
    // Initialize for raw inflate (no zlib/gzip header) - same as wbits=-15 in Python
    int result = inflateInit2(&stream, -15);
    if (result != Z_OK) {
        throw std::runtime_error("Failed to initialize zlib: " + 
                                 std::string(stream.msg ? stream.msg : "unknown error"));
    }
    
    // Perform decompression
    result = inflate(&stream, Z_FINISH);
    
    // Check for successful decompression (Z_STREAM_END or Z_BUF_ERROR with no bytes left to input is ok)
    bool success = (result == Z_STREAM_END) || 
                   (result == Z_BUF_ERROR && stream.avail_in == 0);
    
    // Clean up zlib stream
    inflateEnd(&stream);
    
    if (!success) {
        throw std::runtime_error("Decompression failed: " + 
                                 std::string(stream.msg ? stream.msg : "unknown error"));
    }
    
    // Resize buffer to the actual decompressed size
    out_buffer.resize(stream.total_out);
    
    // Verify decompressed size matches expected size
    if (stream.total_out != expected_len) {
        throw std::runtime_error("Decompressed size (" + 
                                 std::to_string(stream.total_out) + 
                                 ") does not match expected size (" + 
                                 std::to_string(expected_len) + ")");
    }
    
    return out_buffer;
}

/**
 * Implementation of runzip as declared in bk_unzip.h
 */
std::vector<uint8_t> runzip(const uint8_t* in_buffer, size_t in_size) {
    auto [result, _] = runzip_with_leftovers(in_buffer, in_size);
    return result;
}

/**
 * Implementation of runzip_with_leftovers as declared in bk_unzip.h
 */
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> runzip_with_leftovers(
    const uint8_t* in_buffer, size_t in_size) {
    
    // Check buffer size
    if (in_size < 4) {
        throw std::runtime_error("Input buffer too small");
    }
    
    // Prepare output and leftover buffers
    std::vector<uint8_t> out_buffer(16384);  // Start with a reasonable size
    std::vector<uint8_t> unused_data;
    
    // Set up zlib stream
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = static_cast<uInt>(in_size - 4);
    stream.next_in = const_cast<Bytef*>(in_buffer + 4);
    stream.avail_out = static_cast<uInt>(out_buffer.size());
    stream.next_out = out_buffer.data();
    
    // Initialize for raw inflate (no zlib/gzip header)
    int result = inflateInit2(&stream, -15);
    if (result != Z_OK) {
        throw std::runtime_error("Failed to initialize zlib: " + 
                               std::string(stream.msg ? stream.msg : "unknown error"));
    }
    
    // Perform decompression
    result = inflate(&stream, Z_FINISH);
    
    // While we need more output space
    while (result == Z_BUF_ERROR && stream.avail_out == 0) {
        // Double the buffer size
        size_t old_size = out_buffer.size();
        out_buffer.resize(old_size * 2);
        
        // Update the stream to point to the new space
        stream.next_out = out_buffer.data() + old_size;
        stream.avail_out = static_cast<uInt>(old_size);
        
        // Continue inflating
        result = inflate(&stream, Z_FINISH);
    }
    
    // Check for successful decompression (Z_STREAM_END is good, Z_BUF_ERROR with no input bytes left might be OK too)
    bool success = (result == Z_STREAM_END) || 
                   (result == Z_BUF_ERROR && stream.avail_in == 0);
    
    // Save any unused data
    if (stream.avail_in > 0) {
        unused_data.resize(stream.avail_in);
        std::copy(stream.next_in, stream.next_in + stream.avail_in, unused_data.begin());
    }
    
    // Clean up zlib stream
    inflateEnd(&stream);
    
    if (!success) {
        throw std::runtime_error("Decompression failed: " + 
                               std::string(stream.msg ? stream.msg : "unknown error"));
    }
    
    // Resize output buffer to the actual decompressed size
    out_buffer.resize(stream.total_out);
    
    return std::make_pair(out_buffer, unused_data);
}

} // namespace BK64