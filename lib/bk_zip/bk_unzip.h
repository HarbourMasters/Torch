#ifndef BK_UNZIP_H
#define BK_UNZIP_H

#include <vector>
#include <cstdint>
#include <utility>

namespace BK64 {

/**
 * Decompresses a buffer with a Banjo-Kazooie header format
 * 
 * @param in_buffer Input compressed data with BK header (starting with 0x11, 0x72)
 * @param in_size Size of the input buffer
 * @return Decompressed data as vector of bytes
 * @throws std::runtime_error if decompression fails or header is invalid
 */
std::vector<uint8_t> bk_unzip(const uint8_t* in_buffer, size_t in_size);


/**
 * Overloaded functions that accept std::vector input
 */
inline std::vector<uint8_t> bk_unzip(std::vector<uint8_t>& in_buffer) {
    return bk_unzip(in_buffer.data(), in_buffer.size());
}

} // namespace BK64

#endif // BK_UNZIP_H
