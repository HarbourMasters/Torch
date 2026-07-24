#include "yaz0.h"
#include "libmio0/utils.h"
#include <stdlib.h>
#include <string.h>

uint8_t* yaz0_decode(const uint8_t* in, uint32_t* out_size) {
    if (strncmp((const char*)in, "Yaz0", 4) != 0) {
        return NULL;
    }

    uint32_t decompressed_size = read_u32_be(in + 4);
    uint8_t* out = malloc(decompressed_size);
    if (!out) {
        return NULL;
    }

    uint32_t src = YAZ0_HEADER_LENGTH;
    uint32_t dst = 0;
    uint8_t group_head = 0;
    int bits_left = 0;

    while (dst < decompressed_size) {
        if (bits_left == 0) {
            group_head = in[src++];
            bits_left = 8;
        }

        if (group_head & 0x80) {
            /* literal byte */
            out[dst++] = in[src++];
        } else {
            /* back-reference */
            uint8_t b1 = in[src++];
            uint8_t b2 = in[src++];

            uint32_t dist = ((b1 & 0x0F) << 8) | b2;
            uint32_t copy_src = dst - (dist + 1);
            uint32_t count;

            if (b1 >> 4) {
                count = (b1 >> 4) + 2;
            } else {
                count = in[src++] + 0x12;
            }

            for (uint32_t i = 0; i < count; i++) {
                out[dst++] = out[copy_src + i];
            }
        }

        group_head <<= 1;
        bits_left--;
    }

    *out_size = decompressed_size;
    return out;
}
