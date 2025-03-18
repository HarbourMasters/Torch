#ifndef RARE_ZIP_H
#define RARE_ZIP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"{
#endif 

size_t deflate(uint8_t *in_file, size_t in_len, uint8_t *out_file, size_t out_cap);
size_t inflate(uint8_t *in_file, size_t in_len, uint8_t *out_file, size_t out_cap);

size_t zip(uint8_t *in_file, size_t in_len, uint8_t *out_file, size_t out_cap);
size_t unzip(uint8_t *in_file, size_t in_len, uint8_t *out_file, size_t out_cap);

size_t bk_zip(uint8_t *in_file, size_t in_len, uint8_t *out_file, size_t out_cap);
size_t bt_zip(uint8_t *in_file, size_t in_len, uint8_t *out_file, size_t out_cap);
size_t bk_unzip(uint8_t *in_file, size_t in_len, uint8_t *out_file, size_t out_cap);
size_t bt_unzip(uint8_t *in_file, size_t in_len, uint8_t *out_file, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif