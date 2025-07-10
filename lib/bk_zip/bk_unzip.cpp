#include <vector>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <utility>
#include "zlib.h"
#include "bk_unzip.h"

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

} // namespace BK64
