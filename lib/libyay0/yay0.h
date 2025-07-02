#pragma once

#include <stdint.h>

#define YAY0_HEADER_LENGTH 16

extern int32_t yay0_encode(const uint8_t *in_buf, uint32_t length, uint8_t* out_buf);
extern uint8_t* yay0_decode(const uint8_t* in, uint32_t* out_size);