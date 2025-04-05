#ifndef BK_UNZIP_H
#define BK_UNZIP_H

#include <vector>
#include <cstdint>
#include <utility>

namespace BK64{

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
 * Decompresses a raw compressed buffer without BK header
 * The first 4 bytes of the buffer are expected to be the length of decompressed data
 * 
 * @param in_buffer Input compressed data with 4-byte length header
 * @param in_size Size of the input buffer
 * @return Decompressed data as vector of bytes
 * @throws std::runtime_error if decompression fails
 */
std::vector<uint8_t> runzip(const uint8_t* in_buffer, size_t in_size);

/**
 * Decompresses a raw compressed buffer and returns any unused data
 * 
 * @param in_buffer Input compressed data with 4-byte length header
 * @param in_size Size of the input buffer
 * @return A pair of (decompressed data, unused data)
 * @throws std::runtime_error if decompression fails
 */
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> runzip_with_leftovers(
    const uint8_t* in_buffer, size_t in_size);

/**
 * Overloaded functions that accept std::vector input
 */
inline std::vector<uint8_t> bk_unzip(const std::vector<uint8_t>& in_buffer) {
    return bk_unzip(in_buffer.data(), in_buffer.size());
}

inline std::vector<uint8_t> runzip(const std::vector<uint8_t>& in_buffer) {
    return runzip(in_buffer.data(), in_buffer.size());
}

inline std::pair<std::vector<uint8_t>, std::vector<uint8_t>> runzip_with_leftovers(
    const std::vector<uint8_t>& in_buffer) {
    return runzip_with_leftovers(in_buffer.data(), in_buffer.size());
}

} // namespace BK64

#endif // BK_UNZIP_H