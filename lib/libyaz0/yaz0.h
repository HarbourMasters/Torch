#pragma once

#include <stdint.h>

#define YAZ0_HEADER_LENGTH 16

extern uint8_t* yaz0_decode(const uint8_t* in, uint32_t* out_size);
