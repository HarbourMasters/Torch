#include <vector>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <zlib.h>
#include "bk_unzip.h"
#include "spdlog/spdlog.h"

namespace BK64 {

static const char* zlibResultName(int result) {
    switch (result) {
        case Z_OK:
            return "Z_OK";
        case Z_STREAM_END:
            return "Z_STREAM_END";
        case Z_NEED_DICT:
            return "Z_NEED_DICT";
        case Z_ERRNO:
            return "Z_ERRNO";
        case Z_STREAM_ERROR:
            return "Z_STREAM_ERROR";
        case Z_DATA_ERROR:
            return "Z_DATA_ERROR";
        case Z_MEM_ERROR:
            return "Z_MEM_ERROR";
        case Z_BUF_ERROR:
            return "Z_BUF_ERROR";
        case Z_VERSION_ERROR:
            return "Z_VERSION_ERROR";
        default:
            return "UNKNOWN";
    }
}

/**
 * Implementation of bk_unzip as declared in bk_unzip.h
 */
uint8_t* bk_unzip(const uint8_t* in_buffer, uint32_t* size) {
    // Check buffer size
    if (*size < 6) {
        throw std::runtime_error("Input buffer too small");
    }

    // Check for BK magic header
    if (in_buffer[0] != 0x11 || in_buffer[1] != 0x72) {
        throw std::runtime_error("Input buffer does not have BK header");
    }

    // Extract expected uncompressed length (big-endian)
    uint32_t expected_len = (static_cast<uint32_t>(in_buffer[2]) << 24) | (static_cast<uint32_t>(in_buffer[3]) << 16) |
                            (static_cast<uint32_t>(in_buffer[4]) << 8) | static_cast<uint32_t>(in_buffer[5]);

    if (expected_len == 0) {
        *size = 0;
        return (uint8_t*)malloc(1);
    }

    if (expected_len > 8 * 1024 * 1024) {
        throw std::runtime_error("BKZIP expected size too large: " + std::to_string(expected_len) +
                                 " bytes (compressed: " + std::to_string(*size) + ")");
    }

    uint8_t* out_buffer = (uint8_t*)malloc(expected_len);
    memset(out_buffer, 0, expected_len);

    // Set up zlib stream
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = static_cast<uInt>(*size - 6);
    stream.next_in = const_cast<Bytef*>(in_buffer + 6);
    stream.avail_out = static_cast<uInt>(expected_len);
    stream.next_out = out_buffer;

    // Initialize for raw inflate (no zlib/gzip header) - same as wbits=-15 in Python
    int result = inflateInit2(&stream, -15);
    if (result != Z_OK) {
        throw std::runtime_error(std::string("Failed to initialize zlib: ") + zlibResultName(result) +
                                 (stream.msg ? std::string(" (") + stream.msg + ")" : ""));
    }

    // Perform decompression
    result = inflate(&stream, Z_FINISH);

    // Check for successful decompression:
    // - Z_STREAM_END: normal completion
    // - Z_BUF_ERROR with no input remaining: input fully consumed
    bool success = (result == Z_STREAM_END) || (result == Z_BUF_ERROR && stream.avail_in == 0);

    // Clean up zlib stream
    inflateEnd(&stream);

    if (!success) {
        free(out_buffer);
        throw std::runtime_error(std::string("Decompression failed: ") + zlibResultName(result) +
                                 (stream.msg ? std::string(" (") + stream.msg + ")" : "") +
                                 " compressed=" + std::to_string(*size) + " expected=" + std::to_string(expected_len) +
                                 " produced=" + std::to_string(stream.total_out));
    }

    // Resize buffer to the actual decompressed size
    // out_buffer.resize(stream.total_out);
    *size = stream.total_out;

    // Verify decompressed size matches expected size
    if (stream.total_out != expected_len) {
        throw std::runtime_error("Decompressed size (" + std::to_string(stream.total_out) +
                                 ") does not match expected size (" + std::to_string(expected_len) + ")");
    }

    return out_buffer;
}

} // namespace BK64
