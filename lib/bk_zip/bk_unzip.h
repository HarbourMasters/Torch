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
 * @param size Size of the buffer (input as compressed size, output as decompressed size)
 * @return Decompressed data as vector of bytes
 * @throws std::runtime_error if decompression fails or header is invalid
 */
uint8_t* bk_unzip(const uint8_t* in_buffer, uint32_t* size);

} // namespace BK64

#endif // BK_UNZIP_H
